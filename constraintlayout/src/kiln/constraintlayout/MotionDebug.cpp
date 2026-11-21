// MotionLayout's debug overlay (DebugFlags): MotionMeasurer.drawDebug and drawFrame, and
// MotionRenderDebug.basicDraw (MotionRenderDebug.kt): each child's start and end bounds as
// dashed outlines, its path, and diamonds at its key positions, each drawn over a shadow.
// MotionRenderDebug's other modes (relative, cartesian and screen guides with labels) are
// reached only from MotionLayout's view system, not from Compose.

#include "kiln/constraintlayout/MotionInternal.h"

#include "kiln/graphics/Path.h"
#include "kiln/graphics/PathEffect.h"
#include "kiln/ui/Draw.h"

#include <cmath>
#include <vector>

namespace kiln::constraintlayout::detail {

    namespace {

        constexpr int MAX_KEY_FRAMES = 50;
        constexpr int DEBUG_PATH_TICKS_PER_MS = 16;
        constexpr Color kRedColor{ 0xFFFFAA33u };        // -0x55cd
        constexpr Color kKeyframeColor{ 0xFFE0759Au };   // -0x1f8a66
        constexpr Color kShadowColor{ 0x77000000u };
        constexpr float kDiamondSize = 10;
        constexpr float kShadowTranslate = 1;

        // MotionRenderDebug.basicDraw with DRAW_PATH_BASIC.
        void BasicDraw(ui::DrawScope& scope, core::motion::Motion& motionController, int duration, bool drawPath, bool drawTicks) {
            float keyFramePoints[MAX_KEY_FRAMES * 2] = {};
            int pathMode[MAX_KEY_FRAMES] = {};
            const int keyFrameCount = motionController.buildKeyFrames(keyFramePoints, pathMode, nullptr);
            const int frames = duration / DEBUG_PATH_TICKS_PER_MS;
            std::vector<float> points(std::size_t(frames) * 2);
            motionController.buildPath(points.data(), frames);

            auto drawAll = [&](Color paint, Color keyframes, Color fill) {
                if (drawPath) {
                    // canvas.drawLines: segments between consecutive pairs of points.
                    for (std::size_t i = 0; i + 3 < points.size(); i += 4)
                        scope.DrawLine(paint, { points[i], points[i + 1] }, { points[i + 2], points[i + 3] }, 2);
                }
                if (drawTicks) {
                    for (int i = 1; i < keyFrameCount - 1; i++) {
                        const float x = keyFramePoints[i * 2];
                        const float y = keyFramePoints[i * 2 + 1];
                        gfx::Path diamond;
                        diamond.MoveTo(x, y + kDiamondSize);
                        diamond.LineTo(x + kDiamondSize, y);
                        diamond.LineTo(x, y - kDiamondSize);
                        diamond.LineTo(x - kDiamondSize, y);
                        diamond.Close();
                        scope.DrawPath(diamond, fill);
                    }
                    if (points.size() > 1) {
                        const gfx::Stroke stroke{ 2 };
                        scope.DrawCircle(keyframes, 8, ui::Offset{ points[0], points[1] }, 1, ui::DrawStyle::Stroke(stroke));
                        scope.DrawCircle(keyframes, 8, ui::Offset{ points[points.size() - 2], points[points.size() - 1] }, 1,
                                         ui::DrawStyle::Stroke(stroke));
                    }
                }
            };
            scope.Save();
            scope.Translate({ kShadowTranslate, kShadowTranslate });
            drawAll(kShadowColor, kShadowColor, kShadowColor);
            scope.Restore();
            drawAll(kRedColor, kKeyframeColor, kKeyframeColor);
        }

        // MotionMeasurer.drawFrame: the frame's outline, rotated and scaled about its centre.
        void DrawFrame(ui::DrawScope& scope, const WidgetFrame& frame, const std::shared_ptr<const gfx::PathEffect>& pathEffect, Color color) {
            if (frame.isDefaultTransform()) {
                gfx::Stroke stroke{ 3 };
                stroke.pathEffect = pathEffect;
                scope.DrawRect(gfx::Brush::SolidColor(color), { float(frame.left), float(frame.top) }, ui::Size{ float(frame.width()), float(frame.height()) },
                               1, ui::DrawStyle::Stroke(stroke));
                return;
            }
            const float cx = frame.centerX(), cy = frame.centerY();
            const float rotation = std::isnan(frame.rotationZ) ? 0.0f : frame.rotationZ;
            const float scaleX = std::isnan(frame.scaleX) ? 1.0f : frame.scaleX;
            const float scaleY = std::isnan(frame.scaleY) ? 1.0f : frame.scaleY;
            const float radians = rotation * 0.017453292519943295f;
            const float sin = std::sin(radians), cos = std::cos(radians);
            // matrix.preRotate(rotation, cx, cy); matrix.preScale(scaleX, scaleY, cx, cy)
            auto map = [&](float x, float y) {
                const float sx = (x - cx) * scaleX, sy = (y - cy) * scaleY;
                return ui::Offset{ sx * cos - sy * sin + cx, sx * sin + sy * cos + cy };
            };
            const ui::Offset p[4] = { map(float(frame.left), float(frame.top)), map(float(frame.right), float(frame.top)),
                                      map(float(frame.right), float(frame.bottom)), map(float(frame.left), float(frame.bottom)) };
            for (int i = 0; i < 4; i++) scope.DrawLine(color, p[i], p[(i + 1) % 4], 3, gfx::StrokeCap::Butt, pathEffect);
        }

    }

    void MotionMeasurer::DrawDebug(ui::DrawScope& scope, bool drawBounds, bool drawPaths, bool drawKeyPositions) {
        const std::shared_ptr<const gfx::PathEffect> pathEffect = gfx::PathEffect::Dash({ 10, 10 }, 0);
        for (ConstraintWidget* child : m_Root.getChildren()) {
            const WidgetFrame& startFrame = transition.getStart(child);
            const WidgetFrame& endFrame = transition.getEnd(child);
            if (drawBounds) {
                DrawFrame(scope, startFrame, pathEffect, Color::Blue());
                DrawFrame(scope, endFrame, pathEffect, Color::Blue());
                scope.Save();
                scope.Translate({ 2, 2 });
                DrawFrame(scope, startFrame, pathEffect, Color::White());
                DrawFrame(scope, endFrame, pathEffect, Color::White());
                scope.Restore();
            }
            if (startFrame.widget != nullptr) BasicDraw(scope, transition.getMotion(startFrame.widget->stringId), 1000, drawPaths, drawKeyPositions);
        }
    }

}
