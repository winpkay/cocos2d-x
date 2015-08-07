/****************************************************************************
 Copyright (c) 2013 cocos2d-x.org
 
 http://www.cocos2d-x.org
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "network/CCDownloader.h"

#include <curl/curl.h>
#include <curl/easy.h>
#include <cstdio>
#include <cerrno>
#include <thread>

#include "network/CCDownloaderImpl.h"
#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "deprecated/CCString.h"

namespace cocos2d {
namespace network {


#define LOW_SPEED_LIMIT     1L
#define LOW_SPEED_TIME      5L
#define MAX_REDIRS          2
#define DEFAULT_TIMEOUT     5
#define HTTP_CODE_SUPPORT_RESUME    206
#define MAX_WAIT_MSECS 30*1000 /* Wait max. 30 seconds */

#define TEMP_EXT            ".temp"

Downloader::Downloader()
: _connectionTimeout(DEFAULT_TIMEOUT)
, _onError(nullptr)
, _onProgress(nullptr)
, _onSuccess(nullptr)
, _supportResuming(false)
, _downloaderImpl(nullptr)
, _progDatas({})
{
    _fileUtils = FileUtils::getInstance();
    _downloaderImpl = new DownloaderImpl();
}

Downloader::~Downloader()
{
    CC_SAFE_DELETE(_downloaderImpl);
}

int Downloader::getConnectionTimeout()
{
    return _connectionTimeout;
}

void Downloader::setConnectionTimeout(int timeout)
{
    if (timeout >= 0)
        _connectionTimeout = timeout;
}

void Downloader::notifyError(ErrorCode code, const std::string& msg/* ="" */, const std::string& customId/* ="" */, int curle_code/* = CURLE_OK*/, int curlm_code/* = CURLM_OK*/)
{
    std::weak_ptr<Downloader> ptr = shared_from_this();
    Director::getInstance()->getScheduler()->performFunctionInCocosThread([=]{
        if (!ptr.expired())
        {
            std::shared_ptr<Downloader> downloader = ptr.lock();
            if (downloader->_onError != nullptr)
            {
                Error err;
                err.code = code;
                err.curle_code = curle_code;
                err.curlm_code = curlm_code;
                err.message = msg;
                err.customId = customId;
                downloader->_onError(err);
            }
        }
    });
}

void Downloader::notifyError(const std::string& msg, int curlm_code, const std::string& customId/* = ""*/)
{
    notifyError(ErrorCode::CURL_MULTI_ERROR, msg, customId, CURLE_OK, curlm_code);
}

void Downloader::notifyError(const std::string& msg, const std::string& customId, int curle_code)
{
    notifyError(ErrorCode::CURL_EASY_ERROR, msg, customId, curle_code);
}

std::string Downloader::getFileNameFromUrl(const std::string& srcUrl)
{
    // Find file name and file extension
    std::string filename;
    unsigned long found = srcUrl.find_last_of("/\\");
    if (found != std::string::npos)
        filename = srcUrl.substr(found+1);
    return filename;
}

void Downloader::clearBatchDownloadData()
{
    _progDatas.clear();
}

void Downloader::prepareDownload(const std::string& srcUrl, const std::string& storagePath, const std::string& customId, bool resumeDownload, FILE **fp, ProgressData *pData)
{
    pData->customId = customId;
    pData->url = srcUrl;
    pData->downloaded = 0;
    pData->totalToDownload = 0;

    FILE *localFP = nullptr;

    Error err;
    err.customId = customId;
    
    // Asserts
    // Find file name and file extension
    unsigned long found = storagePath.find_last_of("/\\");
    if (found != std::string::npos)
    {
        pData->name = storagePath.substr(found+1);
        pData->path = storagePath.substr(0, found+1);
    }
    else
    {
        err.code = ErrorCode::INVALID_URL;
        err.message = "Invalid url or filename not exist error: " + srcUrl;
        if (this->_onError)
            this->_onError(err);
        *fp = nullptr;
        return;
    }

    // create possible subdirectories
    if (!FileUtils::getInstance()->isDirectoryExist(pData->path))
        FileUtils::getInstance()->createDirectory(pData->path);
    
    // Create a file to save file.
    const std::string outFileName = storagePath + TEMP_EXT;
    if (_supportResuming && resumeDownload && _fileUtils->isFileExist(outFileName))
    {
        localFP = fopen(FileUtils::getInstance()->getSuitableFOpen(outFileName).c_str(), "ab");
    }
    else
    {
        localFP = fopen(FileUtils::getInstance()->getSuitableFOpen(outFileName).c_str(), "wb");
    }
    if (!localFP)
    {
        err.code = ErrorCode::CREATE_FILE;
        err.message = StringUtils::format("Can not create file %s: errno %d", outFileName.c_str(), errno);
        if (this->_onError)
            this->_onError(err);
    }

    *fp = localFP;
}

void Downloader::downloadToBufferAsync(const std::string& srcUrl, unsigned char *buffer, long size, const std::string& customId/* = ""*/)
{
    if (buffer != nullptr)
    {
        auto t = std::thread(&Downloader::downloadToBuffer, this, srcUrl, customId, buffer, size);
        t.detach();
    }
}

void Downloader::downloadToBufferSync(const std::string& srcUrl, unsigned char *buffer, long size, const std::string& customId/* = ""*/)
{
    if (buffer != nullptr)
    {
        downloadToBuffer(srcUrl, customId, buffer, size);
    }
}

void Downloader::downloadToBuffer(const std::string& srcUrl, const std::string& customId, unsigned char* buffer, long size)
{
    CC_ASSERT(buffer && "must not be nill");

    CC_ASSERT(_downloaderImpl && "Cannot instanciate more than one instance of DownloaderImpl");

    DownloadUnit unit;
    unit.srcUrl = srcUrl;
    unit.customId = customId;
    unit.fp = buffer;

    ProgressData data;
    data.customId = customId;
    data.url = srcUrl;
    data.downloaded = 0;
    data.totalToDownload = 0;

    StreamData streamBuffer;
    streamBuffer.buffer = buffer;
    streamBuffer.total = size;
    streamBuffer.offset = 0;

    _downloaderImpl->init(srcUrl);
    int res = _downloaderImpl->performDownload(&unit,
                                               &data,
                                               std::bind(&Downloader::bufferWriteFunc, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
                                               std::bind(&Downloader::downloadProgressFunc, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
                                      );
    // Download pacakge
    if (res != 0)
    {
        std::string msg = StringUtils::format("Unable to download file to buffer: [curl error]%s", _downloaderImpl->getStrError().c_str());
        this->notifyError(msg, customId, res);
    }
    else
    {
        if (std::this_thread::get_id() != Director::getInstance()->getCocos2dThreadId())
        {
            std::weak_ptr<Downloader> ptr = shared_from_this();
            std::shared_ptr<Downloader> shared = ptr.lock();

            Director::getInstance()->getScheduler()->performFunctionInCocosThread([=]{
                if (!ptr.expired())
                {
                    std::shared_ptr<Downloader> downloader = ptr.lock();
                    reportDownloadFinished(data.url, "", data.customId);
                }
            });
        }
        else
        {
            reportDownloadFinished(data.url, "", data.customId);
        }
    }
}

void Downloader::downloadAsync(const std::string& srcUrl, const std::string& storagePath, const std::string& customId/* = ""*/)
{
    auto t = std::thread(&Downloader::downloadToFP, this, srcUrl, customId, storagePath);
    t.detach();
}

void Downloader::downloadSync(const std::string& srcUrl, const std::string& storagePath, const std::string& customId/* = ""*/)
{
    downloadToFP(srcUrl, customId, storagePath);
}

void Downloader::downloadToFP(const std::string& srcUrl, const std::string& customId, const std::string& storagePath)
{
    CC_ASSERT(_downloaderImpl && "Cannot instanciate more than one instance of DownloaderImpl");

    FILE *fp;
    ProgressData data;
    prepareDownload(srcUrl, storagePath, customId, false, &fp, &data);

    DownloadUnit unit;
    unit.srcUrl = srcUrl;
    unit.customId = customId;
    unit.storagePath = storagePath;
    unit.fp = fp;

    _downloaderImpl->init(srcUrl);
    int res = _downloaderImpl->performDownload(&unit,
                                               &data,
                                               std::bind(&Downloader::fileWriteFunc, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
                                               std::bind(&Downloader::downloadProgressFunc, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
                                               );

    if (res != 0)
    {
        // XXX: If this is called from a different thread, will it crash?
        // XXX: Can fileUtils run on a different thread ?
        // XXX: can notifyError run on a different thread ?
        _fileUtils->removeFile(data.path + data.name + TEMP_EXT);
        std::string msg = StringUtils::format("Unable to download file: [curl error]%s", _downloaderImpl->getStrError().c_str());
        this->notifyError(msg, customId, res);
    }
    
    fclose(fp);

    // This can only be done after fclose
    if (res == 0)
    {
        _fileUtils->renameFile(data.path, data.name + TEMP_EXT, data.name);

        if (std::this_thread::get_id() != Director::getInstance()->getCocos2dThreadId())
        {
            std::weak_ptr<Downloader> ptr = shared_from_this();
            std::shared_ptr<Downloader> shared = ptr.lock();

            Director::getInstance()->getScheduler()->performFunctionInCocosThread([=]{
                if (!ptr.expired())
                {
                    std::shared_ptr<Downloader> downloader = ptr.lock();
                    reportDownloadFinished(data.url, data.path + data.name, data.customId);
                }
            });
        }
        else
        {
            reportDownloadFinished(data.url, data.path + data.name, data.customId);
        }
    }
}

void Downloader::batchDownloadAsync(const DownloadUnits& units, const std::string& batchId/* = ""*/)
{
    auto t = std::thread(&Downloader::batchDownloadSync, this, units, batchId);
    t.detach();
}

void Downloader::batchDownloadSync(const DownloadUnits& units, const std::string& batchId/* = ""*/)
{
    // Make sure downloader won't be released
    std::weak_ptr<Downloader> ptr = shared_from_this();
    std::shared_ptr<Downloader> shared = ptr.lock();
    
    if (units.size() != 0)
    {
        _downloaderImpl->init(units.begin()->second.srcUrl);

        _supportResuming = _downloaderImpl->supportsResume();

        // split units in multiple parts if the size is bigger
        // than FOPEN_MAX
        if (units.size() >= FOPEN_MAX)
        {
            int count = 0;
            DownloadUnits group;
            for (auto it = units.cbegin(); it != units.cend(); ++it, ++count)
            {
                if (count == FOPEN_MAX)
                {
                    groupBatchDownload(group);
                    group.clear();
                    count = 0;
                }
                const std::string& key = it->first;
                const DownloadUnit& unit = it->second;
                group.emplace(key, unit);
            }
            if (group.size() > 0)
            {
                groupBatchDownload(group);
            }
        }
        else
        {
            groupBatchDownload(units);
        }
    }
    
    Director::getInstance()->getScheduler()->performFunctionInCocosThread([ptr, batchId]{
        if (!ptr.expired()) {
            std::shared_ptr<Downloader> downloader = ptr.lock();
            auto callback = downloader->getSuccessCallback();
            if (callback != nullptr)
            {
                callback("", "", batchId);
            }
        }
    });
    _supportResuming = false;
}

void Downloader::groupBatchDownload(const DownloadUnits& units)
{
    // static_cast needed since notifyError is overloaded
    auto errorCallback = std::bind( static_cast<void(Downloader::*)(const std::string&, int, const std::string&)>
                          (&Downloader::notifyError), this,
                          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    CC_ASSERT(_progDatas.size() ==0 && "_progsData must be 0");
    _progDatas.resize(units.size());

    int i=0;
    for (const auto& entry: units)
    {
        auto&& unit = entry.second;
        prepareDownload(unit.srcUrl, unit.storagePath, unit.customId, unit.resumeDownload, (FILE**)&unit.fp, &_progDatas[i]);
        i++;
    }
    _downloaderImpl->performBatchDownload(units,
                                          _progDatas,
                                          std::bind(&Downloader::fileWriteFunc, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
                                          std::bind(&Downloader::batchDownloadProgressFunc, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                                          errorCallback
                                          );

    // Check unfinished files and notify errors, succeed files will be renamed from temporary file name to real name
    for (const auto& data: _progDatas) {
        if (data.downloaded < data.totalToDownload || data.totalToDownload == 0)
        {
            this->notifyError(ErrorCode::NETWORK, "Unable to download file", data.customId);
        }
        else
        {
            _fileUtils->renameFile(data.path, data.name + TEMP_EXT, data.name);
        }
    }

    // close opened FPs
    for(const auto& entry: units)
    {
        const auto& unit = entry.second;
        if (unit.fp)
            fclose((FILE*)unit.fp);
    }
    
    clearBatchDownloadData();
}

// callbacks
size_t Downloader::fileWriteFunc(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    CC_ASSERT(userdata && "Invalid userdata");
    DownloadUnit* unit = (DownloadUnit*)userdata;
    FILE *fp = (FILE*)(unit->fp);
    
    CC_ASSERT(fp && "Invalid FP");
    size_t written = fwrite(ptr, size, nmemb, fp);
    return written;
}

size_t Downloader::bufferWriteFunc(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    CC_ASSERT(userdata && "Invalid userdata");
    Downloader::StreamData* streamBuffer = (Downloader::StreamData*)((DownloadUnit*)userdata)->fp;

    CC_ASSERT(streamBuffer && "Invalid streamBuffer");

    size_t written = size * nmemb;
    // Avoid pointer overflow
    if (streamBuffer->offset + written <= static_cast<size_t>(streamBuffer->total))
    {
        memcpy(streamBuffer->buffer + streamBuffer->offset, ptr, written);
        streamBuffer->offset += written;
        return written;
    }
    else return 0;
}

void Downloader::reportDownloadFinished(const std::string& url, const std::string& path, const std::string& customid)
{
    if (_onSuccess != nullptr)
    {
        _onSuccess(url, path, customid);
    }
}
void Downloader::reportProgressFinished(double totalToDownload, double nowDownloaded, const ProgressData* data)
{
    if (_onProgress != nullptr)
    {
        _onProgress(totalToDownload, nowDownloaded, data->url, data->customId);
    }
    reportDownloadFinished(data->url, data->path + data->name, data->customId);
}

void Downloader::reportProgressInProgress(double totalToDownload, double nowDownloaded, const ProgressData* data)
{
    if (_onProgress != nullptr)
    {
        _onProgress(totalToDownload, nowDownloaded, data->url, data->customId);
    }
}

// This is only for batchDownload process, will notify file succeed event in progress function
int Downloader::batchDownloadProgressFunc(void *userdata, double totalToDownload, double nowDownloaded)
{
    CC_ASSERT(userdata && "Invalid userdata");

    ProgressData* ptr = (ProgressData*) userdata;
    if (ptr->totalToDownload == 0)
    {
        ptr->totalToDownload = totalToDownload;
    }

    if (ptr->downloaded != nowDownloaded)
    {
        ptr->downloaded = nowDownloaded;

        if (nowDownloaded == totalToDownload)
        {
            if (std::this_thread::get_id() != Director::getInstance()->getCocos2dThreadId())
            {
                std::weak_ptr<Downloader> _this = shared_from_this();
                ProgressData copyData = *ptr;
                Director::getInstance()->getScheduler()->performFunctionInCocosThread([=]{
                    if (!_this.expired())
                    {
                        this->reportProgressFinished(totalToDownload, nowDownloaded, &copyData);
                    }
                });
            }
            else
            {
                reportProgressFinished(totalToDownload, nowDownloaded, ptr);
            }
        }
        else
        {
            if (std::this_thread::get_id() != Director::getInstance()->getCocos2dThreadId())
            {
                std::weak_ptr<Downloader> _this = shared_from_this();
                ProgressData copyData = *ptr;
                Director::getInstance()->getScheduler()->performFunctionInCocosThread([=]{
                    if (!_this.expired())
                    {
                        reportProgressInProgress(totalToDownload, nowDownloaded, &copyData);
                    }
                });
            }
            else
            {
                reportProgressInProgress(totalToDownload, nowDownloaded, ptr);
            }
        }
    }

    return 0;
}

// Compare to batchDownloadProgressFunc, this only handles progress information notification
int Downloader::downloadProgressFunc(void *userdata, double totalToDownload, double nowDownloaded)
{
    CC_ASSERT(userdata && "Invalid userdata");

    ProgressData* ptr = (ProgressData*)userdata;
    if (ptr->totalToDownload == 0)
    {
        ptr->totalToDownload = totalToDownload;
    }

    if (ptr->downloaded != nowDownloaded)
    {
        ptr->downloaded = nowDownloaded;
        ProgressData data = *ptr;
        std::weak_ptr<Downloader> _this = shared_from_this();

        Director::getInstance()->getScheduler()->performFunctionInCocosThread([=]{
            if (!_this.expired())
            {
                std::shared_ptr<Downloader> downloader = _this.lock();
                
                auto callback = downloader->getProgressCallback();
                if (callback != nullptr)
                {
                    callback(totalToDownload, nowDownloaded, data.url, data.customId);
                }
            }
        });
    }
    
    return 0;
}


} //  namespace network
}  // namespace cocos2d

