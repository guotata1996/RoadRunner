#pragma once

#include "world.h"
#include "action_defs.h"
#include "road_graphics.h"

#include "Transform3D.h"
#include <vector>
#include <map>
#include <spdlog/spdlog.h>

class RoadDrawingSession
{
public:
    RoadDrawingSession();

    /*return false if force complete*/
    virtual bool Update(const RoadRunner::MouseAction&);
    virtual bool Update(const RoadRunner::KeyPressAction&);

    /*return false to abort change*/
    virtual bool Complete() = 0;

    virtual ~RoadDrawingSession();

    void SetHighlightTo(std::shared_ptr<RoadRunner::Road>);

protected:
    class CustomCursorItem: RoadRunner::TemporaryGraphics
    {
    public:
        CustomCursorItem();
        ~CustomCursorItem();

        void EnableHighlight(int level);
        void SetTranslation(odr::Vec3D);

    private:
        void DrawGroundGrids();

        int highlightLevel;
        odr::Vec3D translation;

        TemporaryGraphics groundGrids;
    };

    enum SnapResult
    {
        Snap_Nothing,
        Snap_Line,
        Snap_Point
    };

    static std::shared_ptr<RoadRunner::Road> GetPointerRoad();
    static odr::Vec2D CursorAtHeight(double);

    float SnapDistFromScale() const;

    double GetAdjustedS(bool* onSegmentBoundary = nullptr) const;

    void BeginPickingProfile();
    void ContinuePickingProfile();
    void EndPickingProfile();
    bool PickProfileMode() const { return !beginPickingRoad.expired(); }

    static bool IsProfileChangePoint(const std::shared_ptr<RoadRunner::Road>&, double s);

    static void UpdateEndMarkings();

    World* world;

    std::unique_ptr<CustomCursorItem> cursorItem;

private:
    std::weak_ptr<RoadRunner::Road> highlighted;

    double beginPickingS;
    std::weak_ptr<RoadRunner::Road> beginPickingRoad;
};

class RoadCreationSession : public RoadDrawingSession
{
public:
    virtual bool Update(const RoadRunner::MouseAction&) override;
    virtual bool Update(const RoadRunner::KeyPressAction&) override;

    virtual bool Complete() override;

protected:
    // Record extend / join
    virtual SnapResult SnapFirstPointToExisting(odr::Vec2D&);
    virtual SnapResult SnapLastPointToExisting(odr::Vec2D&);

    std::weak_ptr<RoadRunner::Road> extendFromStart;
    double extendFromStartS;
    std::weak_ptr<RoadRunner::Road> joinAtEnd;
    double joinAtEndS;

    virtual odr::Vec2D ExtendFromDir() const;
    virtual odr::Vec2D JoinAtEndDir() const;
    double CursorElevation() const;

    odr::RefLine ResultRefLine() const;

    virtual RoadRunner::type_t PreviewRightOffsetX2() const;
    virtual RoadRunner::type_t PreviewLeftOffsetX2() const;

private:
    class DirectionHandle
    {
    public:
        DirectionHandle(odr::Vec3D center, double angle);
        //~DirectionHandle();

        bool Update(const RoadRunner::MouseAction& act);
        double Rotation() const;

    private:
        bool rayHitLocal(odr::Vec2D&) const;
        void UpdateGraphics();

        const odr::Vec3D center;
        double angle;

        double dragging = false;
        double deltaRotation;

        std::optional<RoadRunner::HintPolyGraphics> graphicsItem;
        const double InnerRadius = 4;
        const double OuterRadius = 6;
    };

    struct StagedGeometry
    {
        std::unique_ptr<odr::RoadGeometry> geo;
        double endEleveation;
    };

    SnapResult SnapCursor(odr::Vec2D&);
    void UpdateFlexGeometry();

    std::vector<StagedGeometry> stagedGeometries;
    RoadRunner::LanePlan stagedLeftPlan, stagedRightPlan;

    std::optional<odr::Vec2D> startPos; // Can be on blank or extend from
    double startElevation;

    void GenerateHintLines(const odr::RefLine& refLine,
        odr::Line3D&, odr::Line3D&, odr::Line3D&);

    void UpdateStagedFromGeometries();

    std::unique_ptr<odr::RoadGeometry> flexGeo;
    double flexEndElevation;

    std::unique_ptr<DirectionHandle> directionHandle;

    std::optional<RoadRunner::HintLineGraphics> stagedRefLinePreview;
    std::optional<RoadRunner::HintLineGraphics> stagedBoundaryPreview;
    std::optional<RoadRunner::HintLineGraphics> flexRefLinePreview;
    std::optional<RoadRunner::HintLineGraphics> flexBoundaryPreview;
};


class LanesCreationSession : public RoadCreationSession
{
public:
    LanesCreationSession();

    virtual bool Complete() override;

protected:
    virtual SnapResult SnapFirstPointToExisting(odr::Vec2D&) override;
    virtual SnapResult SnapLastPointToExisting(odr::Vec2D&) override;
    virtual odr::Vec2D ExtendFromDir() const override;
    virtual odr::Vec2D JoinAtEndDir() const override;

    virtual RoadRunner::type_t PreviewRightOffsetX2() const override;
    virtual RoadRunner::type_t PreviewLeftOffsetX2() const override;

private:
    bool ValidateSnap() const;

    RoadRunner::type_t rLanes; // If lLanes == 0, becomes required lanes
    RoadRunner::type_t lLanes; // If 0, single-directional
    RoadRunner::type_t rOffsetX2, lOffsetX2;

    // Offset of ramp w.r.t interface provider
    // Only != 0 when single-direction ramp
    uint8_t startLanesSkip, endLanesSkip;

    // Offset of narrower (not interface-provider) half of split result w.r.t. interface provider
    // Only != 0 at left-exit from single-directional origin
    uint8_t startSplitOffset, endSplitOffset;

    int startSide, endSide; // Only != 0 when single-direction ramp

    bool startFullyMatch, endFullyMatch; // If true, extend instead of creating direct junction
};

class RoadDestroySession : public RoadDrawingSession
{
public:
    virtual bool Update(const RoadRunner::MouseAction&) override;
    virtual bool Update(const RoadRunner::KeyPressAction&) override;

    virtual bool Complete() override;

protected:
    odr::Line3D hintPolygonLeft, hintPolygonRight;

    std::optional<RoadRunner::HintLineGraphics> hintItemLeft, hintItemRight;

    std::weak_ptr<RoadRunner::Road> targetRoad;
    std::unique_ptr<double> s1, s2;

private:
    void UpdateHint();
};

class RoadModificationSession : public RoadDestroySession
{
public:
    RoadModificationSession();

    virtual bool Update(const RoadRunner::MouseAction&) override;

    virtual bool Complete() override;
};