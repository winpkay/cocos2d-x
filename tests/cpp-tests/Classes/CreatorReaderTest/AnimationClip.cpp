/****************************************************************************
 Copyright (c) 2017 Chukong Technologies Inc.

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

#include "AnimationClip.h"

USING_NS_CCR;

AnimationClip* AnimationClip::create()
{
    auto animClip = new (std::nothrow) AnimationClip;
    if (animClip && animClip->init()) {
        animClip->autorelease();
    }
    return nullptr;
}

bool AnimationClip::init()
{
    return false;
}

AnimationClip::AnimationClip()
: _name("")
, _uuid("")
, _speed(0)
, _sample(0)
, _duration(0)
, _objFlags(0)
, _wrapMode(WrapMode::Default)
{
}

AnimationClip::~AnimationClip()
{
}

void AnimationClip::setName(const std::string& name)
{
    _name = name;
}

void AnimationClip::setUUID(const std::string& uuid)
{
    _uuid = uuid;
}

void AnimationClip::setDuration(float duration)
{
    _duration = duration;
}

void AnimationClip::setSample(float sample)
{
    _sample = sample;
}

void AnimationClip::setSpeed(float speed)
{
    _speed = speed;
}

void AnimationClip::setWrapMode(WrapMode wrapMode)
{
    _wrapMode = wrapMode;
}

void AnimationClip::setAnimProperties(const AnimProperties& properties)
{
    _animProperties = properties;
}
