#include "kiln/constraintlayout/core/motion/Motion.h"

#include "kiln/constraintlayout/core/LinearSystem.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/key/MotionKeyAttributes.h"
#include "kiln/constraintlayout/core/motion/utils/KeyCycleOscillator.h"
#include "kiln/constraintlayout/core/motion/utils/SplineSet.h"
#include "kiln/constraintlayout/core/motion/utils/TimeCycleSplineSet.h"
#include "kiln/constraintlayout/core/motion/utils/VelocityMatrix.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace kiln::constraintlayout::core::motion {

    namespace {
        constexpr int UNSET = -1;
        constexpr int SPLINE_STRING = -1;

        // The anonymous DifferentialInterpolator of getInterpolator(SPLINE_STRING, ...).
        class EasingInterpolator final : public DifferentialInterpolator {
        public:
            explicit EasingInterpolator(std::shared_ptr<Easing> easing) : mEasing(std::move(easing)) {}
            float getInterpolation(float x) override {
                mX = x;
                return float(mEasing->get(x));
            }
            float getVelocity() override { return float(mEasing->getDiff(mX)); }

        private:
            std::shared_ptr<Easing> mEasing;
            float mX = 0;
        };

        std::shared_ptr<DifferentialInterpolator> getInterpolator(int type, const std::string& interpolatorString, int) {
            if (type == SPLINE_STRING) return std::make_shared<EasingInterpolator>(Easing::getInterpolator(interpolatorString));
            return nullptr;
        }

        std::string secondField(const std::string& attribute) {
            const std::size_t comma = attribute.find(',');
            const std::size_t next = attribute.find(',', comma + 1);
            return attribute.substr(comma + 1, next == std::string::npos ? std::string::npos : next - comma - 1);
        }

        template<typename V>
        V* lookup(const std::unique_ptr<JavaHashMap<std::shared_ptr<V>>>& map, const char* key) {
            if (map == nullptr) return nullptr;
            const std::shared_ptr<V>* found = map->find(key);
            return found == nullptr ? nullptr : found->get();
        }

        std::string javaFloat(float v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", double(v));
            std::string s = buf;
            if (s.find_first_of(".en") == std::string::npos) s += ".0";
            return s;
        }
    }

    void Motion::setupRelative() {
        if (mRelativeMotion == nullptr) return;
        mStartMotionPath.setupRelative(mRelativeMotion, mRelativeMotion->mStartMotionPath);
        mEndMotionPath.setupRelative(mRelativeMotion, mRelativeMotion->mEndMotionPath);
    }

    void Motion::getCenter(double p, float* pos, float* vel) {
        double position[4] = {};
        double velocity[4] = {};
        mSpline[0]->getPos(p, position);
        mSpline[0]->getSlope(p, velocity);
        vel[0] = vel[1] = 0;
        mStartMotionPath.getCenter(p, mInterpolateVariables, position, pos, velocity, vel);
    }

    double Motion::easedPosition(float position) const {
        double p = position;
        std::shared_ptr<Easing> easing = mStartMotionPath.mKeyFrameEasing;
        float start = 0;
        float end = NAN;
        for (const MotionPaths& frame : mMotionPaths) {
            if (frame.mKeyFrameEasing != nullptr) {
                if (frame.mTime < position) {
                    easing = frame.mKeyFrameEasing;
                    start = frame.mTime;
                } else if (std::isnan(end)) {
                    end = frame.mTime;
                }
            }
        }
        if (easing != nullptr) {
            if (std::isnan(end)) end = 1.0f;
            float offset = (position - start) / (end - start);
            offset = float(easing->get(offset));
            p = offset * (end - start) + start;
        }
        return p;
    }

    void Motion::buildPath(float* points, int pointCount) {
        const float mils = 1.0f / (pointCount - 1);
        SplineSet* trans_x = lookup(mAttributesMap, MotionKey::TRANSLATION_X);
        SplineSet* trans_y = lookup(mAttributesMap, MotionKey::TRANSLATION_Y);
        KeyCycleOscillator* osc_x = lookup(mCycleMap, MotionKey::TRANSLATION_X);
        KeyCycleOscillator* osc_y = lookup(mCycleMap, MotionKey::TRANSLATION_Y);
        for (int i = 0; i < pointCount; i++) {
            float position = i * mils;
            if (mStaggerScale != 1.0f) {
                if (position < mStaggerOffset) position = 0;
                if (position > mStaggerOffset && position < 1.0) {
                    position -= mStaggerOffset;
                    position *= mStaggerScale;
                    position = std::min(position, 1.0f);
                }
            }
            const double p = easedPosition(position);
            mSpline[0]->getPos(p, mInterpolateData.data());
            if (mArcSpline != nullptr && !mInterpolateData.empty()) mArcSpline->getPos(p, mInterpolateData.data());
            mStartMotionPath.getCenter(p, mInterpolateVariables, mInterpolateData.data(), points, i * 2);
            if (osc_x != nullptr) points[i * 2] += osc_x->get(position);
            else if (trans_x != nullptr) points[i * 2] += trans_x->get(position);
            if (osc_y != nullptr) points[i * 2 + 1] += osc_y->get(position);
            else if (trans_y != nullptr) points[i * 2 + 1] += trans_y->get(position);
        }
    }

    const std::vector<double>& Motion::getPos(double position) {
        mSpline[0]->getPos(position, mInterpolateData.data());
        if (mArcSpline != nullptr && !mInterpolateData.empty()) mArcSpline->getPos(position, mInterpolateData.data());
        return mInterpolateData;
    }

    void Motion::buildBounds(float* bounds, int pointCount) {
        const float mils = 1.0f / (pointCount - 1);
        for (int i = 0; i < pointCount; i++) {
            float position = i * mils;
            if (mStaggerScale != 1.0f) {
                if (position < mStaggerOffset) position = 0;
                if (position > mStaggerOffset && position < 1.0) {
                    position -= mStaggerOffset;
                    position *= mStaggerScale;
                    position = std::min(position, 1.0f);
                }
            }
            const double p = easedPosition(position);
            mSpline[0]->getPos(p, mInterpolateData.data());
            if (mArcSpline != nullptr && !mInterpolateData.empty()) mArcSpline->getPos(p, mInterpolateData.data());
            mStartMotionPath.getBounds(mInterpolateVariables, mInterpolateData.data(), bounds, i * 2);
        }
    }

    float Motion::getPreCycleDistance() {
        const int pointCount = 100;
        float points[2] = {};
        float sum = 0;
        const float mils = 1.0f / (pointCount - 1);
        double x = 0, y = 0;
        for (int i = 0; i < pointCount; i++) {
            const float position = i * mils;
            const double p = easedPosition(position);
            mSpline[0]->getPos(p, mInterpolateData.data());
            mStartMotionPath.getCenter(p, mInterpolateVariables, mInterpolateData.data(), points, 0);
            if (i > 0) sum += float(std::hypot(y - points[1], x - points[0]));
            x = points[0];
            y = points[1];
        }
        return sum;
    }

    MotionKeyPosition* Motion::getPositionKeyframe(int layoutWidth, int layoutHeight, float x, float y) {
        FloatRect start;
        start.left = mStartMotionPath.mX;
        start.top = mStartMotionPath.mY;
        start.right = start.left + mStartMotionPath.mWidth;
        start.bottom = start.top + mStartMotionPath.mHeight;
        FloatRect end;
        end.left = mEndMotionPath.mX;
        end.top = mEndMotionPath.mY;
        end.right = end.left + mEndMotionPath.mWidth;
        end.bottom = end.top + mEndMotionPath.mHeight;
        for (const auto& key : mKeyList) {
            if (key->mType != MotionKeyPosition::KEY_TYPE) continue;
            auto* kp = static_cast<MotionKeyPosition*>(key.get());
            if (kp->intersects(layoutWidth, layoutHeight, start, end, x, y)) return kp;
        }
        return nullptr;
    }

    int Motion::buildKeyFrames(float* keyFrames, int* mode, int* pos) {
        if (keyFrames == nullptr) return 0;
        int count = 0;
        const std::vector<double> time = mSpline[0]->getTimePoints();
        if (mode != nullptr) {
            for (const MotionPaths& keyFrame : mMotionPaths) mode[count++] = keyFrame.mMode;
            count = 0;
        }
        if (pos != nullptr) {
            for (const MotionPaths& keyFrame : mMotionPaths) pos[count++] = JavaInt(100 * keyFrame.mPosition);
            count = 0;
        }
        for (const double t : time) {
            mSpline[0]->getPos(t, mInterpolateData.data());
            mStartMotionPath.getCenter(t, mInterpolateVariables, mInterpolateData.data(), keyFrames, count);
            count += 2;
        }
        return count / 2;
    }

    int Motion::buildKeyBounds(float* keyBounds, int* mode) {
        if (keyBounds == nullptr) return 0;
        int count = 0;
        const std::vector<double> time = mSpline[0]->getTimePoints();
        if (mode != nullptr) {
            for (const MotionPaths& keyFrame : mMotionPaths) mode[count++] = keyFrame.mMode;
            count = 0;
        }
        for (const double t : time) {
            mSpline[0]->getPos(t, mInterpolateData.data());
            mStartMotionPath.getBounds(mInterpolateVariables, mInterpolateData.data(), keyBounds, count);
            count += 2;
        }
        return count / 2;
    }

    int Motion::getAttributeValues(const std::string& attributeType, float* points, int length) {
        const std::shared_ptr<SplineSet>* spline = mAttributesMap ? mAttributesMap->find(attributeType) : nullptr;
        if (spline == nullptr || *spline == nullptr) return -1;
        // Upstream divides ints (j / (length - 1)), so every point but the last samples 0.
        for (int j = 0; j < length; j++) points[j] = (*spline)->get(float(j / (length - 1)));
        return length;
    }

    void Motion::buildRect(float p, float* path, int offset) {
        p = getAdjustedPosition(p, nullptr);
        mSpline[0]->getPos(p, mInterpolateData.data());
        mStartMotionPath.getRect(mInterpolateVariables, mInterpolateData.data(), path, offset);
    }

    void Motion::buildRectangles(float* path, int pointCount) {
        const float mils = 1.0f / (pointCount - 1);
        for (int i = 0; i < pointCount; i++) {
            float position = i * mils;
            position = getAdjustedPosition(position, nullptr);
            mSpline[0]->getPos(position, mInterpolateData.data());
            mStartMotionPath.getRect(mInterpolateVariables, mInterpolateData.data(), path, i * 8);
        }
    }

    float Motion::getKeyFrameParameter(int type, float x, float y) const {
        const float dx = mEndMotionPath.mX - mStartMotionPath.mX;
        const float dy = mEndMotionPath.mY - mStartMotionPath.mY;
        const float startCenterX = mStartMotionPath.mX + mStartMotionPath.mWidth / 2;
        const float startCenterY = mStartMotionPath.mY + mStartMotionPath.mHeight / 2;
        const float hypotenuse = float(std::hypot(double(dx), double(dy)));
        if (hypotenuse < 0.0000001) return NAN;
        const float vx = x - startCenterX;
        const float vy = y - startCenterY;
        const float distFromStart = float(std::hypot(double(vx), double(vy)));
        if (distFromStart == 0) return 0;
        const float pathDistance = vx * dx + vy * dy;
        switch (type) {
            case PATH_PERCENT: return pathDistance / hypotenuse;
            case PATH_PERPENDICULAR: return float(std::sqrt(double(hypotenuse * hypotenuse - pathDistance * pathDistance)));
            case HORIZONTAL_PATH_X: return vx / dx;
            case HORIZONTAL_PATH_Y: return vy / dx;
            case VERTICAL_PATH_X: return vx / dy;
            case VERTICAL_PATH_Y: return vy / dy;
        }
        return 0;
    }

    void Motion::insertKey(MotionPaths point) {
        // Drop the last point at the same position, as upstream.
        for (auto it = mMotionPaths.rbegin(); it != mMotionPaths.rend(); ++it) {
            if (point.mPosition == it->mPosition) {
                mMotionPaths.erase(std::next(it).base());
                break;
            }
        }
        const auto at = std::lower_bound(mMotionPaths.begin(), mMotionPaths.end(), point,
                                         [](const MotionPaths& a, const MotionPaths& b) { return a.compareTo(b) < 0; });
        if (at == mMotionPaths.begin() && !mMotionPaths.empty() && at->compareTo(point) == 0)
            std::fprintf(stderr, "MotionController:  KeyPath position \"%g\" outside of range\n", double(point.mPosition));
        mMotionPaths.insert(at, std::move(point));
    }

    void Motion::setup(int parentWidth, int parentHeight, float, int64_t currentTime) {
        JavaHashSet timeCycleAttributes;
        JavaHashSet splineAttributes;
        JavaHashSet cycleAttributes;
        JavaHashMap<int> interpolation;
        std::vector<MotionKeyTrigger*> triggerList;
        bool hasTriggers = false;
        setupRelative();
        if (mPathMotionArc != UNSET && mStartMotionPath.mPathMotionArc == UNSET) mStartMotionPath.mPathMotionArc = mPathMotionArc;
        mStartPoint.different(mEndPoint, splineAttributes);
        for (const auto& key : mKeyList) {
            switch (key->mType) {
                case MotionKeyPosition::KEY_TYPE: {
                    auto& keyPath = static_cast<MotionKeyPosition&>(*key);
                    insertKey(MotionPaths(parentWidth, parentHeight, keyPath, mStartMotionPath, mEndMotionPath));
                    if (keyPath.mCurveFit != UNSET) mCurveFitType = keyPath.mCurveFit;
                    break;
                }
                case MotionKeyCycle::KEY_TYPE: key->getAttributeNames(cycleAttributes); break;
                case MotionKeyTimeCycle::KEY_TYPE: key->getAttributeNames(timeCycleAttributes); break;
                case MotionKeyTrigger::KEY_TYPE:
                    hasTriggers = true;
                    triggerList.push_back(static_cast<MotionKeyTrigger*>(key.get()));
                    break;
                default:
                    key->setInterpolation(interpolation);
                    key->getAttributeNames(splineAttributes);
                    break;
            }
        }
        if (hasTriggers) mKeyTriggers = triggerList;

        auto customVarList = [&](const std::string& attribute) {
            auto attrList = std::make_shared<KeyFrameArray::CustomVar>();
            const std::string customAttributeName = secondField(attribute);
            for (const auto& key : mKeyList) {
                if (const CustomVariable* customAttribute = key->mCustom.find(customAttributeName))
                    attrList->append(key->mFramePosition, *customAttribute);
            }
            return attrList;
        };

        if (!splineAttributes.empty()) {
            mAttributesMap = std::make_unique<SplineMap>();
            for (const std::string& attribute : splineAttributes.keys()) {
                std::shared_ptr<SplineSet> splineSets;
                if (attribute.rfind("CUSTOM,", 0) == 0) splineSets = SplineSet::makeCustomSplineSet(attribute, customVarList(attribute));
                else splineSets = SplineSet::makeSpline(attribute, currentTime);
                if (splineSets == nullptr) continue;
                splineSets->setType(attribute);
                mAttributesMap->put(attribute, splineSets);
            }
            for (const auto& key : mKeyList)
                if (key->mType == MotionKeyAttributes::KEY_TYPE) key->addValues(*mAttributesMap);
            mStartPoint.addValues(*mAttributesMap, 0);
            mEndPoint.addValues(*mAttributesMap, 100);
            for (const std::string& spline : mAttributesMap->keys()) {
                int curve = CurveFit::SPLINE;
                if (const int* boxed = interpolation.find(spline)) curve = *boxed;
                if (std::shared_ptr<SplineSet>* splineSet = mAttributesMap->find(spline); splineSet && *splineSet) (*splineSet)->setup(curve);
            }
        }

        if (!timeCycleAttributes.empty()) {
            if (mTimeCycleAttributesMap == nullptr) mTimeCycleAttributesMap = std::make_unique<TimeCycleMap>();
            // Upstream builds a SplineSet per attribute here and never stores it, so the time
            // cycle map stays empty.
            for (const std::string& attribute : timeCycleAttributes.keys()) {
                if (mTimeCycleAttributesMap->contains(attribute)) continue;
                std::shared_ptr<SplineSet> splineSets;
                if (attribute.rfind("CUSTOM,", 0) == 0) splineSets = SplineSet::makeCustomSplineSet(attribute, customVarList(attribute));
                else splineSets = SplineSet::makeSpline(attribute, currentTime);
                if (splineSets == nullptr) continue;
                splineSets->setType(attribute);
            }
            for (const auto& key : mKeyList)
                if (key->mType == MotionKeyTimeCycle::KEY_TYPE) static_cast<MotionKeyTimeCycle&>(*key).addTimeValues(*mTimeCycleAttributesMap);
            for (const std::string& spline : mTimeCycleAttributesMap->keys()) {
                int curve = CurveFit::SPLINE;
                if (const int* boxed = interpolation.find(spline)) curve = *boxed;
                (*mTimeCycleAttributesMap->find(spline))->setup(curve);
            }
        }

        std::vector<MotionPaths*> points(2 + mMotionPaths.size());
        int count = 1;
        points[0] = &mStartMotionPath;
        points.back() = &mEndMotionPath;
        if (!mMotionPaths.empty() && mCurveFitType == MotionKey::UNSET) mCurveFitType = CurveFit::SPLINE;
        for (MotionPaths& point : mMotionPaths) points[std::size_t(count++)] = &point;
        const int variables = 18;
        JavaHashSet attributeNameSet;
        for (const std::string& s : mEndMotionPath.mCustomAttributes.keys()) {
            if (mStartMotionPath.mCustomAttributes.contains(s) && !splineAttributes.contains("CUSTOM," + s)) attributeNameSet.add(s);
        }
        mAttributeNames = attributeNameSet.keys();
        mAttributeInterpolatorCount.assign(mAttributeNames.size(), 0);
        for (std::size_t i = 0; i < mAttributeNames.size(); i++) {
            const std::string& attributeName = mAttributeNames[i];
            for (MotionPaths* point : points) {
                if (const CustomVariable* attribute = point->mCustomAttributes.find(attributeName)) {
                    mAttributeInterpolatorCount[i] += attribute->numberOfInterpolatedValues();
                    break;
                }
            }
        }
        const bool arcMode = points[0]->mPathMotionArc != UNSET;
        std::vector<char> maskStorage(std::size_t(variables) + mAttributeNames.size(), 0);
        bool* mask = reinterpret_cast<bool*>(maskStorage.data());
        for (std::size_t i = 1; i < points.size(); i++) points[i]->different(*points[i - 1], mask, mAttributeNames, arcMode);
        count = 0;
        for (std::size_t i = 1; i < maskStorage.size(); i++)
            if (mask[i]) count++;
        mInterpolateVariables.assign(std::size_t(count), 0);
        const int varLen = std::max(2, count);
        mInterpolateData.assign(std::size_t(varLen), 0);
        mInterpolateVelocity.assign(std::size_t(varLen), 0);
        count = 0;
        for (std::size_t i = 1; i < maskStorage.size(); i++)
            if (mask[i]) mInterpolateVariables[std::size_t(count++)] = int(i);
        std::vector<std::vector<double>> splineData(points.size(), std::vector<double>(mInterpolateVariables.size()));
        std::vector<double> timePoint(points.size());
        for (std::size_t i = 0; i < points.size(); i++) {
            points[i]->fillStandard(splineData[i].data(), mInterpolateVariables);
            timePoint[i] = points[i]->mTime;
        }
        mSpline.assign(1 + mAttributeNames.size(), nullptr);
        for (std::size_t i = 0; i < mAttributeNames.size(); i++) {
            int pointCount = 0;
            std::vector<std::vector<double>> splinePoints;
            std::vector<double> timePoints;
            const std::string& name = mAttributeNames[i];
            for (MotionPaths* point : points) {
                if (!point->hasCustomData(name)) continue;
                if (splinePoints.empty()) {
                    timePoints.assign(points.size(), 0);
                    splinePoints.assign(points.size(), std::vector<double>(std::size_t(point->getCustomDataCount(name))));
                }
                timePoints[std::size_t(pointCount)] = point->mTime;
                point->getCustomData(name, splinePoints[std::size_t(pointCount)].data(), 0);
                pointCount++;
            }
            timePoints.resize(std::size_t(pointCount));
            splinePoints.resize(std::size_t(pointCount));
            mSpline[i + 1] = CurveFit::get(mCurveFitType, timePoints, splinePoints);
        }
        mSpline[0] = CurveFit::get(mCurveFitType, timePoint, splineData);
        if (points[0]->mPathMotionArc != UNSET) {
            const std::size_t size = points.size();
            std::vector<int> mode(size);
            std::vector<double> time(size);
            std::vector<std::vector<double>> values(size, std::vector<double>(2));
            for (std::size_t i = 0; i < size; i++) {
                mode[i] = points[i]->mPathMotionArc;
                time[i] = points[i]->mTime;
                values[i][0] = points[i]->mX;
                values[i][1] = points[i]->mY;
            }
            mArcSpline = CurveFit::getArc(mode, time, values);
        }
        float distance = NAN;
        mCycleMap = std::make_unique<KeyCycleOscillatorMap>();
        for (const std::string& attribute : cycleAttributes.keys()) {
            std::shared_ptr<KeyCycleOscillator> cycle = KeyCycleOscillator::makeWidgetCycle(attribute);
            if (cycle == nullptr) continue;
            if (cycle->variesByPath() && std::isnan(distance)) distance = getPreCycleDistance();
            cycle->setType(attribute);
            mCycleMap->put(attribute, cycle);
        }
        for (const auto& key : mKeyList)
            if (key->mType == MotionKeyCycle::KEY_TYPE) static_cast<MotionKeyCycle&>(*key).addCycleValues(*mCycleMap);
        mCycleMap->forEach([&](const std::string&, const std::shared_ptr<KeyCycleOscillator>& cycle) { cycle->setup(distance); });
    }

    std::string Motion::toString() const {
        return " start: x: " + javaFloat(mStartMotionPath.mX) + " y: " + javaFloat(mStartMotionPath.mY) + " end: x: " + javaFloat(mEndMotionPath.mX) +
               " y: " + javaFloat(mEndMotionPath.mY);
    }

    void Motion::readView(MotionPaths& motionPaths) {
        motionPaths.setBounds(float(mView->getX()), float(mView->getY()), float(mView->getWidth()), float(mView->getHeight()));
    }

    void Motion::setStart(MotionWidget& mw) {
        mStartMotionPath.mTime = 0;
        mStartMotionPath.mPosition = 0;
        mStartMotionPath.setBounds(float(mw.getX()), float(mw.getY()), float(mw.getWidth()), float(mw.getHeight()));
        mStartMotionPath.applyParameters(mw);
        mStartPoint.setState(mw);
        if (const auto& p = mw.getWidgetFrame().getMotionProperties()) p->applyDelta(*this);
    }

    void Motion::setEnd(MotionWidget& mw) {
        mEndMotionPath.mTime = 1;
        mEndMotionPath.mPosition = 1;
        readView(mEndMotionPath);
        mEndMotionPath.setBounds(float(mw.getLeft()), float(mw.getTop()), float(mw.getWidth()), float(mw.getHeight()));
        mEndMotionPath.applyParameters(mw);
        mEndPoint.setState(mw);
    }

    void Motion::setStartState(const ViewState& rect, MotionWidget& v, int rotation, int preWidth, int preHeight) {
        mStartMotionPath.mTime = 0;
        mStartMotionPath.mPosition = 0;
        Rect r;
        switch (rotation) {
            case 2: {
                const int cx = rect.left + rect.right;
                const int cy = rect.top + rect.bottom;
                r.left = preHeight - (cy + rect.width()) / 2;
                r.top = (cx - rect.height()) / 2;
                r.right = r.left + rect.width();
                r.bottom = r.top + rect.height();
                break;
            }
            case 1: {
                const int cx = rect.left + rect.right;
                const int cy = rect.top + rect.bottom;
                r.left = (cy - rect.width()) / 2;
                r.top = preWidth - (cx + rect.height()) / 2;
                r.right = r.left + rect.width();
                r.bottom = r.top + rect.height();
                break;
            }
        }
        mStartMotionPath.setBounds(float(r.left), float(r.top), float(r.width()), float(r.height()));
        mStartPoint.setState(r, v, rotation, rect.rotation);
    }

    void Motion::rotate(const Rect& rect, Rect& out, int rotation, int preHeight, int preWidth) const {
        const int cx = rect.left + rect.right;
        const int cy = rect.top + rect.bottom;
        switch (rotation) {
            case MotionWidget::ROTATE_PORTRATE_OF_LEFT:
            case MotionWidget::ROTATE_LEFT_OF_PORTRATE:
                out.left = preHeight - (cy + rect.width()) / 2;
                out.top = (cx - rect.height()) / 2;
                break;
            case MotionWidget::ROTATE_PORTRATE_OF_RIGHT:
                out.left = (cy - rect.width()) / 2;
                out.top = preWidth - (cx + rect.height()) / 2;
                break;
            case MotionWidget::ROTATE_RIGHT_OF_PORTRATE:
                out.left = rect.height() / 2 + rect.top - cx / 2;
                out.top = preWidth - (cx + rect.height()) / 2;
                break;
            default: return;
        }
        out.right = out.left + rect.width();
        out.bottom = out.top + rect.height();
    }

    void Motion::setBothStates(MotionWidget& v) {
        mStartMotionPath.mTime = 0;
        mStartMotionPath.mPosition = 0;
        mNoMovement = true;
        mStartMotionPath.setBounds(float(v.getX()), float(v.getY()), float(v.getWidth()), float(v.getHeight()));
        mEndMotionPath.setBounds(float(v.getX()), float(v.getY()), float(v.getWidth()), float(v.getHeight()));
        mStartPoint.setState(v);
        mEndPoint.setState(v);
    }

    float Motion::getAdjustedPosition(float position, float* velocity) {
        if (velocity != nullptr) {
            velocity[0] = 1;
        } else if (mStaggerScale != 1.0) {
            if (position < mStaggerOffset) position = 0;
            if (position > mStaggerOffset && position < 1.0) {
                position -= mStaggerOffset;
                position *= mStaggerScale;
                position = std::min(position, 1.0f);
            }
        }
        float adjusted = position;
        std::shared_ptr<Easing> easing = mStartMotionPath.mKeyFrameEasing;
        float start = 0;
        float end = NAN;
        for (const MotionPaths& frame : mMotionPaths) {
            if (frame.mKeyFrameEasing != nullptr) {
                if (frame.mTime < position) {
                    easing = frame.mKeyFrameEasing;
                    start = frame.mTime;
                } else if (std::isnan(end)) {
                    end = frame.mTime;
                }
            }
        }
        if (easing != nullptr) {
            if (std::isnan(end)) end = 1.0f;
            const float offset = (position - start) / (end - start);
            const float new_offset = float(easing->get(offset));
            adjusted = new_offset * (end - start) + start;
            if (velocity != nullptr) velocity[0] = float(easing->getDiff(offset));
        }
        return adjusted;
    }

    bool Motion::interpolate(MotionWidget& child, float globalPosition, int64_t, KeyCache*) {
        float position = getAdjustedPosition(globalPosition, nullptr);
        if (mQuantizeMotionSteps != UNSET) {
            const float steps = 1.0f / mQuantizeMotionSteps;
            const float jump = float(std::floor(position / steps)) * steps;
            float section = std::fmod(position, steps) / steps;
            if (!std::isnan(mQuantizeMotionPhase)) section = std::fmod(section + mQuantizeMotionPhase, 1.0f);
            if (mQuantizeMotionInterpolator != nullptr) section = mQuantizeMotionInterpolator->getInterpolation(section);
            else section = section > 0.5 ? 1 : 0;
            position = section * steps + jump;
        }
        if (mAttributesMap != nullptr)
            mAttributesMap->forEach([&](const std::string&, const std::shared_ptr<SplineSet>& aSpline) { aSpline->setProperty(child, position); });
        if (!mSpline.empty()) {
            mSpline[0]->getPos(position, mInterpolateData.data());
            mSpline[0]->getSlope(position, mInterpolateVelocity.data());
            if (mArcSpline != nullptr && !mInterpolateData.empty()) {
                mArcSpline->getPos(position, mInterpolateData.data());
                mArcSpline->getSlope(position, mInterpolateVelocity.data());
            }
            if (!mNoMovement)
                mStartMotionPath.setView(position, child, mInterpolateVariables, mInterpolateData.data(), mInterpolateVelocity, nullptr);
            if (mTransformPivotTarget != UNSET) {
                if (mTransformPivotView == nullptr) {
                    MotionWidget* layout = child.getParent();
                    mTransformPivotView = layout->findViewById(mTransformPivotTarget);
                }
                if (mTransformPivotView != nullptr) {
                    const float cy = (mTransformPivotView->getTop() + mTransformPivotView->getBottom()) / 2.0f;
                    const float cx = (mTransformPivotView->getLeft() + mTransformPivotView->getRight()) / 2.0f;
                    if (child.getRight() - child.getLeft() > 0 && child.getBottom() - child.getTop() > 0) {
                        child.setPivotX(cx - child.getLeft());
                        child.setPivotY(cy - child.getTop());
                    }
                }
            }
            for (std::size_t i = 1; i < mSpline.size(); i++) {
                mSpline[i]->getPos(position, mValuesBuff.data());
                mStartMotionPath.mCustomAttributes.find(mAttributeNames[i - 1])->setInterpolatedValue(child, mValuesBuff.data());
            }
            if (mStartPoint.mVisibilityMode == MotionWidget::VISIBILITY_MODE_NORMAL) {
                if (position <= 0.0f) child.setVisibility(mStartPoint.mVisibility);
                else if (position >= 1.0f) child.setVisibility(mEndPoint.mVisibility);
                else if (mEndPoint.mVisibility != mStartPoint.mVisibility) child.setVisibility(MotionWidget::VISIBLE);
            }
            for (MotionKeyTrigger* trigger : mKeyTriggers) trigger->conditionallyFire(position, child);
        } else {
            const float float_l = mStartMotionPath.mX + (mEndMotionPath.mX - mStartMotionPath.mX) * position;
            const float float_t = mStartMotionPath.mY + (mEndMotionPath.mY - mStartMotionPath.mY) * position;
            const float float_width = mStartMotionPath.mWidth + (mEndMotionPath.mWidth - mStartMotionPath.mWidth) * position;
            const float float_height = mStartMotionPath.mHeight + (mEndMotionPath.mHeight - mStartMotionPath.mHeight) * position;
            const int l = JavaInt(0.5f + float_l);
            const int t = JavaInt(0.5f + float_t);
            const int r = JavaInt(0.5f + float_l + float_width);
            const int b = JavaInt(0.5f + float_t + float_height);
            child.layout(l, t, r, b);
        }
        if (mCycleMap != nullptr) {
            mCycleMap->forEach([&](const std::string&, const std::shared_ptr<KeyCycleOscillator>& osc) {
                if (PathRotateSet* prs = osc->asPathRotateSet()) prs->setPathRotate(child, position, mInterpolateVelocity[0], mInterpolateVelocity[1]);
                else osc->setProperty(child, position);
            });
        }
        return false;
    }

    void Motion::getDpDt(float position, float locationX, float locationY, float* anchorDpDt) {
        position = getAdjustedPosition(position, mVelocity);
        if (!mSpline.empty()) {
            mSpline[0]->getSlope(position, mInterpolateVelocity.data());
            mSpline[0]->getPos(position, mInterpolateData.data());
            const float v = mVelocity[0];
            for (double& d : mInterpolateVelocity) d *= v;
            if (mArcSpline != nullptr) {
                if (!mInterpolateData.empty()) {
                    mArcSpline->getPos(position, mInterpolateData.data());
                    mArcSpline->getSlope(position, mInterpolateVelocity.data());
                    mStartMotionPath.setDpDt(locationX, locationY, anchorDpDt, mInterpolateVariables, mInterpolateVelocity.data(), mInterpolateData.data());
                }
                return;
            }
            mStartMotionPath.setDpDt(locationX, locationY, anchorDpDt, mInterpolateVariables, mInterpolateVelocity.data(), mInterpolateData.data());
            return;
        }
        const float dleft = mEndMotionPath.mX - mStartMotionPath.mX;
        const float dTop = mEndMotionPath.mY - mStartMotionPath.mY;
        const float dWidth = mEndMotionPath.mWidth - mStartMotionPath.mWidth;
        const float dHeight = mEndMotionPath.mHeight - mStartMotionPath.mHeight;
        const float dRight = dleft + dWidth;
        const float dBottom = dTop + dHeight;
        anchorDpDt[0] = dleft * (1 - locationX) + dRight * locationX;
        anchorDpDt[1] = dTop * (1 - locationY) + dBottom * locationY;
    }

    void Motion::getPostLayoutDvDp(float position, int width, int height, float locationX, float locationY, float* anchorDpDt) {
        position = getAdjustedPosition(position, mVelocity);
        SplineSet* trans_x = lookup(mAttributesMap, MotionKey::TRANSLATION_X);
        SplineSet* trans_y = lookup(mAttributesMap, MotionKey::TRANSLATION_Y);
        SplineSet* rotation = lookup(mAttributesMap, MotionKey::ROTATION);
        SplineSet* scale_x = lookup(mAttributesMap, MotionKey::SCALE_X);
        SplineSet* scale_y = lookup(mAttributesMap, MotionKey::SCALE_Y);
        KeyCycleOscillator* osc_x = lookup(mCycleMap, MotionKey::TRANSLATION_X);
        KeyCycleOscillator* osc_y = lookup(mCycleMap, MotionKey::TRANSLATION_Y);
        KeyCycleOscillator* osc_r = lookup(mCycleMap, MotionKey::ROTATION);
        KeyCycleOscillator* osc_sx = lookup(mCycleMap, MotionKey::SCALE_X);
        KeyCycleOscillator* osc_sy = lookup(mCycleMap, MotionKey::SCALE_Y);
        VelocityMatrix vmat;
        auto fill = [&] {
            vmat.clear();
            vmat.setRotationVelocity(rotation, position);
            vmat.setTranslationVelocity(trans_x, trans_y, position);
            vmat.setScaleVelocity(scale_x, scale_y, position);
            vmat.setRotationVelocity(osc_r, position);
            vmat.setTranslationVelocity(osc_x, osc_y, position);
            vmat.setScaleVelocity(osc_sx, osc_sy, position);
        };
        fill();
        if (mArcSpline != nullptr) {
            if (!mInterpolateData.empty()) {
                mArcSpline->getPos(position, mInterpolateData.data());
                mArcSpline->getSlope(position, mInterpolateVelocity.data());
                mStartMotionPath.setDpDt(locationX, locationY, anchorDpDt, mInterpolateVariables, mInterpolateVelocity.data(), mInterpolateData.data());
            }
            vmat.applyTransform(locationX, locationY, width, height, anchorDpDt);
            return;
        }
        if (!mSpline.empty()) {
            position = getAdjustedPosition(position, mVelocity);
            mSpline[0]->getSlope(position, mInterpolateVelocity.data());
            mSpline[0]->getPos(position, mInterpolateData.data());
            const float v = mVelocity[0];
            for (double& d : mInterpolateVelocity) d *= v;
            mStartMotionPath.setDpDt(locationX, locationY, anchorDpDt, mInterpolateVariables, mInterpolateVelocity.data(), mInterpolateData.data());
            vmat.applyTransform(locationX, locationY, width, height, anchorDpDt);
            return;
        }
        const float dleft = mEndMotionPath.mX - mStartMotionPath.mX;
        const float dTop = mEndMotionPath.mY - mStartMotionPath.mY;
        const float dWidth = mEndMotionPath.mWidth - mStartMotionPath.mWidth;
        const float dHeight = mEndMotionPath.mHeight - mStartMotionPath.mHeight;
        const float dRight = dleft + dWidth;
        const float dBottom = dTop + dHeight;
        anchorDpDt[0] = dleft * (1 - locationX) + dRight * locationX;
        anchorDpDt[1] = dTop * (1 - locationY) + dBottom * locationY;
        fill();
        vmat.applyTransform(locationX, locationY, width, height, anchorDpDt);
    }

    int Motion::getDrawPath() const {
        int mode = mStartMotionPath.mDrawPath;
        for (const MotionPaths& keyFrame : mMotionPaths) mode = std::max(mode, keyFrame.mDrawPath);
        return std::max(mode, mEndMotionPath.mDrawPath);
    }

    std::string Motion::name() const { return mView->getName(); }

    void Motion::positionKeyframe(MotionWidget& view, MotionKeyPosition& key, float x, float y, std::string* attribute, float* value) {
        FloatRect start;
        start.left = mStartMotionPath.mX;
        start.top = mStartMotionPath.mY;
        start.right = start.left + mStartMotionPath.mWidth;
        start.bottom = start.top + mStartMotionPath.mHeight;
        FloatRect end;
        end.left = mEndMotionPath.mX;
        end.top = mEndMotionPath.mY;
        end.right = end.left + mEndMotionPath.mWidth;
        end.bottom = end.top + mEndMotionPath.mHeight;
        key.positionAttributes(view, start, end, x, y, attribute, value);
    }

    int Motion::getKeyFramePositions(int* type, float* pos) {
        int i = 0;
        int count = 0;
        for (const auto& key : mKeyList) {
            type[i++] = key->mFramePosition + 1000 * key->mType;
            const float time = key->mFramePosition / 100.0f;
            mSpline[0]->getPos(time, mInterpolateData.data());
            mStartMotionPath.getCenter(time, mInterpolateVariables, mInterpolateData.data(), pos, count);
            count += 2;
        }
        return i;
    }

    int Motion::getKeyFrameInfo(int type, int* info) {
        auto bits = [](float f) {
            if (std::isnan(f)) return 0x7fc00000;   // Float.floatToIntBits canonical NaN
            int32_t i;
            std::memcpy(&i, &f, sizeof i);
            return int(i);
        };
        int count = 0;
        int cursor = 0;
        float pos[2] = {};
        for (const auto& key : mKeyList) {
            if (key->mType != type && type == -1) continue;   // as upstream
            const int len = cursor;
            info[cursor] = 0;
            info[++cursor] = key->mType;
            info[++cursor] = key->mFramePosition;
            const float time = key->mFramePosition / 100.0f;
            mSpline[0]->getPos(time, mInterpolateData.data());
            mStartMotionPath.getCenter(time, mInterpolateVariables, mInterpolateData.data(), pos, 0);
            info[++cursor] = bits(pos[0]);
            info[++cursor] = bits(pos[1]);
            if (key->mType == MotionKeyPosition::KEY_TYPE) {
                const auto& kp = static_cast<const MotionKeyPosition&>(*key);
                info[++cursor] = kp.mPositionType;
                info[++cursor] = bits(kp.mPercentX);
                info[++cursor] = bits(kp.mPercentY);
            }
            cursor++;
            info[len] = cursor - len;
            count++;
        }
        return count;
    }

    bool Motion::setValue(int id, int value) {
        switch (id) {
            case PositionType::TYPE_PATH_MOTION_ARC: setPathMotionArc(value); return true;
            case MotionType::TYPE_QUANTIZE_MOTIONSTEPS: mQuantizeMotionSteps = value; return true;
            case TransitionType::TYPE_AUTO_TRANSITION: return true;
        }
        return false;
    }

    bool Motion::setValue(int id, float value) {
        if (MotionType::TYPE_QUANTIZE_MOTION_PHASE == id) {
            mQuantizeMotionPhase = value;
            return true;
        }
        if (MotionType::TYPE_STAGGER == id) {
            mMotionStagger = value;
            return true;
        }
        return false;
    }

    bool Motion::setValue(int id, const std::string& value) {
        if (TransitionType::TYPE_INTERPOLATOR == id || MotionType::TYPE_QUANTIZE_INTERPOLATOR_TYPE == id) {
            mQuantizeMotionInterpolator = getInterpolator(SPLINE_STRING, value, 0);
            return true;
        }
        if (MotionType::TYPE_ANIMATE_RELATIVE_TO == id) {
            mStartMotionPath.mAnimateRelativeTo = value;
            return true;
        }
        return false;
    }

}
