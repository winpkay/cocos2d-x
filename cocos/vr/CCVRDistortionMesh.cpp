/****************************************************************************
 Copyright (c) 2016 Google Inc.
 Copyright (c) 2016 Chukong Technologies Inc.

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

#include "vr/CCVRDistortionMesh.h"
#include "vr/CCVRDistortion.h"
#include "math/Vec2.h"

NS_CC_BEGIN

DistortionMesh::DistortionMesh(Distortion *distortionRed,
                               Distortion *distortionGreen,
                               Distortion *distortionBlue,
                               float screenWidth, float screenHeight,
                               float xEyeOffsetScreen, float yEyeOffsetScreen,
                               float textureWidth, float textureHeight,
                               float xEyeOffsetTexture, float yEyeOffsetTexture,
                               float viewportXTexture, float viewportYTexture,
                               float viewportWidthTexture, float viewportHeightTexture,
                               bool vignetteEnabled)
: _indices(-1)
, _arrayBufferID(-1)
, _elementBufferID(-1)
{
    GLfloat vertexData[14400];

    int vertexOffset = 0;

    const int rows = 40;
    const int cols = 40;

    const float vignetteSizeTanAngle = 0.05f;

    for (int row = 0; row < rows; row++)
    {
        for (int col = 0; col < cols; col++)
        {
            const float uTextureBlue = col / 39.0f * (viewportWidthTexture / textureWidth) + viewportXTexture / textureWidth;
            const float vTextureBlue = row / 39.0f * (viewportHeightTexture / textureHeight) + viewportYTexture / textureHeight;

            const float xTexture = uTextureBlue * textureWidth - xEyeOffsetTexture;
            const float yTexture = vTextureBlue * textureHeight - yEyeOffsetTexture;
            const float rTexture = sqrtf(xTexture * xTexture + yTexture * yTexture);

            const float textureToScreenBlue = (rTexture > 0.0f) ? distortionBlue->distortInverse(rTexture) / rTexture : 1.0f;

            const float xScreen = xTexture * textureToScreenBlue;
            const float yScreen = yTexture * textureToScreenBlue;

            const float uScreen = (xScreen + xEyeOffsetScreen) / screenWidth;
            const float vScreen = (yScreen + yEyeOffsetScreen) / screenHeight;
            const float rScreen = rTexture * textureToScreenBlue;

            const float screenToTextureGreen = (rScreen > 0.0f) ? distortionGreen->distortionFactor(rScreen) : 1.0f;
            const float uTextureGreen = (xScreen * screenToTextureGreen + xEyeOffsetTexture) / textureWidth;
            const float vTextureGreen = (yScreen * screenToTextureGreen + yEyeOffsetTexture) / textureHeight;

            const float screenToTextureRed = (rScreen > 0.0f) ? distortionRed->distortionFactor(rScreen) : 1.0f;
            const float uTextureRed = (xScreen * screenToTextureRed + xEyeOffsetTexture) / textureWidth;
            const float vTextureRed = (yScreen * screenToTextureRed + yEyeOffsetTexture) / textureHeight;

            const float vignetteSizeTexture = vignetteSizeTanAngle / textureToScreenBlue;

            const float dxTexture = xTexture + xEyeOffsetTexture - clampf(xTexture + xEyeOffsetTexture,
                                                                         viewportXTexture + vignetteSizeTexture,
                                                                         viewportXTexture + viewportWidthTexture - vignetteSizeTexture);
            const float dyTexture = yTexture + yEyeOffsetTexture - clampf(yTexture + yEyeOffsetTexture,
                                                                         viewportYTexture + vignetteSizeTexture,
                                                                         viewportYTexture + viewportHeightTexture - vignetteSizeTexture);
            const float drTexture = sqrtf(dxTexture * dxTexture + dyTexture * dyTexture);

            float vignette = 1.0f;
            if (vignetteEnabled)
            {
                vignette = 1.0f - clampf(drTexture / vignetteSizeTexture, 0.0f, 1.0f);
            }

            vertexData[(vertexOffset + 0)] = 2.0f * uScreen - 1.0f;
            vertexData[(vertexOffset + 1)] = 2.0f * vScreen - 1.0f;
            vertexData[(vertexOffset + 2)] = vignette;
            vertexData[(vertexOffset + 3)] = uTextureRed;
            vertexData[(vertexOffset + 4)] = vTextureRed;
            vertexData[(vertexOffset + 5)] = uTextureGreen;
            vertexData[(vertexOffset + 6)] = vTextureGreen;
            vertexData[(vertexOffset + 7)] = uTextureBlue;
            vertexData[(vertexOffset + 8)] = vTextureBlue;

            vertexOffset += 9;
        }
    }

    _indices = 3158;
    GLshort indexData[_indices];

    int indexOffset = 0;
    vertexOffset = 0;
    for (int row = 0; row < rows-1; row++)
    {
        if (row > 0)
        {
            indexData[indexOffset] = indexData[(indexOffset - 1)];
            indexOffset++;
        }
        for (int col = 0; col < cols; col++)
        {
            if (col > 0)
            {
                if (row % 2 == 0)
                {
                    vertexOffset++;
                }
                else
                {
                    vertexOffset--;
                }
            }
            indexData[(indexOffset++)] = vertexOffset;
            indexData[(indexOffset++)] = (vertexOffset + 40);
        }
        vertexOffset += 40;
    }

    GLuint bufferIDs[2] = { 0, 0 };
    glGenBuffers(2, bufferIDs);
    _arrayBufferID = bufferIDs[0];
    _elementBufferID = bufferIDs[1];

    glBindBuffer(GL_ARRAY_BUFFER, _arrayBufferID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _elementBufferID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indexData), indexData, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

//    GLCheckForError();
}

NS_CC_END
