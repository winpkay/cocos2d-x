/****************************************************************************
 Copyright (c) 2015 Chukong Technologies Inc.

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

 Ideas taken from:
 - GamePlay3D: http://gameplay3d.org/
 - OGRE3D: http://www.ogre3d.org/
 - Qt3D: http://qt-project.org/
 ****************************************************************************/

#include "renderer/CCMaterial.h"
#include "renderer/CCTechnique.h"
#include "renderer/CCPass.h"
#include "renderer/CCGLProgramState.h"
#include "renderer/CCTextureCache.h"
#include "base/CCDirector.h"
#include "platform/CCFileUtils.h"

#include "json/document.h"

static const float MATERIAL_FORMAT_VERSION = 1.0;
static const char* MATERIAL_TYPE = "material";

NS_CC_BEGIN

Material* Material::createWithFilename(const std::string& filepath)
{
    auto validfilename = FileUtils::getInstance()->fullPathForFilename(filepath);

    if (validfilename.size() > 0)
        return new (std::nothrow) Material(validfilename);

    return nullptr;
}

Material* Material::createWithGLStateProgram(GLProgramState* programState)
{
    return new (std::nothrow) Material(programState);
}

Material::Material(cocos2d::GLProgramState *state)
{
    auto technique = Technique::createWithGLProgramState(state);
    _techniques.pushBack(technique);

    // weak pointer
    _currentTechnique = technique;
}

Material::Material(const std::string& validfilename)
: _currentTechnique(nullptr)
, _techniques()
{
    Data data = FileUtils::getInstance()->getDataFromFile(validfilename);
    char* bytes = (char*)data.getBytes();

    rapidjson::Document document;
    document.ParseInsitu<0>(bytes);

    if (document.HasParseError())
    {
        CCLOG("GetParseError %s\n", document.GetParseError());
        return;
    }

    CCASSERT(document.IsObject(), "Invalid JSON file");

    if (! parseMetadata(document)) {
        CCLOG("Error parsing Material metadata");
        return;
    }

    parseProperties(document);
}

bool Material::parseMetadata(const rapidjson::Document& jsonDocument)
{
    bool broken = false;

    const auto& metadata = jsonDocument["metadata"];
    if (metadata.IsObject())
    {
        auto version = metadata["version"].GetDouble();
        broken |= std::floor(version) != std::floor(MATERIAL_FORMAT_VERSION);

        auto type = metadata["type"].GetString();
        broken |= strcmp(type, MATERIAL_TYPE) != 0;
    }

    return !broken;
}

bool Material::parseProperties(const rapidjson::Document& jsonDocument)
{
    auto name = jsonDocument["name"].GetString();
    setName(name);

    auto& techniquesJSON = jsonDocument["techniques"];
    CCASSERT(techniquesJSON.IsArray(), "Invalid Techniques");

    for (rapidjson::SizeType i = 0; i < techniquesJSON.Size(); i++) {
        auto& techniqueJSON = techniquesJSON[i];
        parseTechnique(techniqueJSON);
    }

    return true;
}

bool Material::parseTechnique(const rapidjson::GenericValue<rapidjson::UTF8<> >& techniqueJSON)
{
    CCASSERT(techniqueJSON.IsObject(), "Invalid type for Technique. It must be an object");

    auto technique = Technique::create();
    _techniques.pushBack(technique);

    // first one is the default one
    if (!_currentTechnique)
        _currentTechnique = technique;

    // name
    if (techniqueJSON.HasMember("name"))
        setName(techniqueJSON["name"].GetString());

    // passes
    auto& passesJSON = techniqueJSON["passes"];
    CCASSERT(passesJSON.IsArray(), "Invalid type for 'passes'");

    for (rapidjson::SizeType i = 0; i < passesJSON.Size(); i++) {
        auto& passJSON = passesJSON[i];
        parsePass(technique, passJSON);
    }

    return true;
}

bool Material::parsePass(Technique* technique, const rapidjson::GenericValue<rapidjson::UTF8<> >& passJSON)
{
    auto pass = Pass::create();
    technique->addPass(pass);

    // Textures
    if (passJSON.HasMember("textures")) {
        auto& texturesJSON = passJSON["textures"];
        CCASSERT(texturesJSON.IsArray(), "Invalid type for 'textures'");

        for (rapidjson::SizeType i = 0; i < texturesJSON.Size(); i++) {
            auto& textureJSON = texturesJSON[i];
            parseTexture(pass, textureJSON);
        }
    }

    return true;
}

static const char* getOptionalString(const rapidjson::GenericValue<rapidjson::UTF8<> >& json, const char* key, const char* defaultValue)
{
    if (json.HasMember(key)) {
        return json[key].GetString();
    }
    return defaultValue;
}

bool Material::parseTexture(Pass* pass, const rapidjson::GenericValue<rapidjson::UTF8<> >& textureJSON)
{
    CCASSERT(textureJSON.IsObject(), "Invalid type for Texture. It must be an object");

    // required
    auto filename = textureJSON["path"].GetString();

    auto texture = Director::getInstance()->getTextureCache()->addImage(filename);
    if (!texture) {
        CCLOG("Invalid filepath");
        return false;
    }

    // optionals

    {
        // mipmap
        bool usemipmap = false;
        const char* mipmap = getOptionalString(textureJSON, "mipmap", "false");
        if (mipmap && strcasecmp(mipmap, "true")) {
            texture->generateMipmap();
            usemipmap = true;
        }

        // valid options: REPEAT, CLAMP
        const char* wrapS = getOptionalString(textureJSON, "wrapS", "CLAMP");

        // valid options: REPEAT, CLAMP
        const char* wrapT = getOptionalString(textureJSON, "wrapT", "CLAMP");

        // valid options: NEAREST, LINEAR, NEAREST_MIPMAP_NEAREST, LINEAR_MIPMAP_NEAREST, NEAREST_MIPMAP_LINEAR, LINEAR_MIPMAP_LINEAR
        const char* minFilter = getOptionalString(textureJSON, "minFilter", mipmap ? "LINEAR_MIPMAP_NEAREST" : "LINEAR");

        // valid options: NEAREST, LINEAR
        const char* maxFilter = getOptionalString(textureJSON, "magFilter", "LINEAR");
    }


    pass->_textures.pushBack(texture);
    return true;
}


bool Material::parseRenderState(Pass* pass, const rapidjson::GenericValue<rapidjson::UTF8<> >& renderState)
{
    return true;
}

void Material::setName(const std::string&name)
{
    _name = name;
}

std::string Material::getName() const
{
    return _name;
}

Material::Material()
{
}

Material::~Material()
{
}

Technique* Material::getTechnique() const
{
    return _currentTechnique;
}
Technique* Material::getTechniqueByName(const std::string& name)
{
    for(const auto& technique : _techniques) {
        if( technique->getName() == name)
            return technique;
    }
    return nullptr;
}

Technique* Material::getTechniqueByIndex(ssize_t index)
{
    CC_ASSERT(index>=0 && index<_techniques.size() && "Invalid size");

    return _techniques.at(index);
}

void Material::addTechnique(Technique* technique)
{
    _techniques.pushBack(technique);
}

void Material::setTechnique(const std::string& techniqueName)
{
    auto technique = getTechniqueByName(techniqueName);
    if (technique)
        _currentTechnique = technique;
}

ssize_t Material::getTechniqueCount() const
{
    return _techniques.size();
}

NS_CC_END
