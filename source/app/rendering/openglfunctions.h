/* Copyright © 2013-2020 Graphia Technologies Ltd.
 *
 * This file is part of Graphia.
 *
 * Graphia is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Graphia is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Graphia.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OPENGLFUNCTIONS_H
#define OPENGLFUNCTIONS_H

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLExtensions>
#include <QString>

#include <memory>

class OpenGLFunctions : public QOpenGLFunctions_3_3_Core
{
public:
    void resolveOpenGLFunctions();

    bool hasSampleShading() const { return _sampleShadingExtension != nullptr; }
    inline void glMinSampleShading(GLfloat value)
    {
        if(hasSampleShading())
            _sampleShadingExtension->glMinSampleShadingARB(value);
    }

    static bool hasOpenGLSupport();
    static QString vendor();
    static QString info();

    static void setDefaultFormat();
    static QSurfaceFormat defaultFormat();

private:
    std::unique_ptr<QOpenGLExtension_ARB_sample_shading> _sampleShadingExtension;
};

// MacOS's glext.h is rubbish
#ifndef GL_ARB_sample_shading
#define GL_ARB_sample_shading 1
#define GL_SAMPLE_SHADING_ARB             0x8C36
#define GL_MIN_SAMPLE_SHADING_VALUE_ARB   0x8C37
typedef void (APIENTRYP PFNGLMINSAMPLESHADINGARBPROC) (GLfloat value);
#ifdef GL_GLEXT_PROTOTYPES
GLAPI void APIENTRY glMinSampleShadingARB (GLfloat value);
#endif
#endif /* GL_ARB_sample_shading */

#endif // OPENGLFUNCTIONS_H
