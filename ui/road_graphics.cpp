#include "road_graphics.h"
#include "mainwindow.h"


#include <QGraphicsSceneMouseEvent>
#include <qgraphicsscene.h>
#include <qvector2d.h>

#include "spdlog/spdlog.h"
#include "stats.h"

extern QGraphicsScene* g_scene;


namespace RoadRunner
{
    SectionGraphics::SectionGraphics(std::shared_ptr<RoadRunner::Road> _road,
        const odr::LaneSection& laneSection,
        double s_begin, double s_end) : 
        sBegin(s_begin), sEnd(s_end), Length(std::abs(s_begin - s_end)),
        road(_road)
    {
        g_scene->addItem(this);
        Create(laneSection);
    }

    SectionGraphics::~SectionGraphics()
    {
        g_scene->removeItem(this);
    }

    void SectionGraphics::EnableHighlight(bool enabled)
    {
        setZValue(enabled ? 1 : 0);
        for (auto laneSegment : allLaneGraphics)
        {
            if (laneSegment != nullptr)
            {
                laneSegment->EnableHighlight(enabled);
            }
        }
        refLineHint->setVisible(enabled);
    }

    void SectionGraphics::Create(const odr::LaneSection& laneSection)
    {
        odr::Road& gen = road.lock()->generated;
        bool biDirRoad = gen.rr_profile.HasSide(-1) && gen.rr_profile.HasSide(1);
        const double sMin = std::min(sBegin, sEnd);
        const double sMax = std::max(sBegin, sEnd);
        for (const auto& id2Lane : laneSection.id_to_lane)
        {
            const auto& lane = id2Lane.second;
            if (lane.type == "median" || lane.type == "driving")
            {
                odr::Line3D innerBorder, outerBorder;
                gen.get_lane_border_line(lane, sMin, sMax, 0.1f, outerBorder, innerBorder);
                auto aggregateBorder = outerBorder;
                aggregateBorder.insert(aggregateBorder.end(), innerBorder.rbegin(), innerBorder.rend());
                QPolygonF poly = LineToPoly(aggregateBorder);
                const int laneID = id2Lane.first;
                int laneIDWhenReversed = 0;
                if (biDirRoad)
                {
                    if (lane.type == "median")
                    {
                        assert(laneID == 1);
                        laneIDWhenReversed = 1;
                    }
                    else
                    {
                        laneIDWhenReversed = -laneID + 1;
                    }
                }
                else
                {
                    laneIDWhenReversed = -laneID;
                }
                auto laneSegmentItem = new LaneGraphics(poly, outerBorder, innerBorder,
                    laneID, laneIDWhenReversed, lane.type, this);
                allLaneGraphics.push_back(laneSegmentItem);

                for (const auto& markingGroup : lane.roadmark_groups)
                {
                    for (const auto& marking : markingGroup.roadmark_lines)
                    {
                        std::vector<odr::Line3D> lines;
                        std::vector<std::string> colors;
                        bool refInner = std::abs(marking.t_offset) < RoadRunner::LaneWidth / 2;
                        double refOffset = marking.t_offset;
                        if (!refInner)
                        {
                            refOffset += lane.id < 0 ? RoadRunner::LaneWidth : -RoadRunner::LaneWidth;
                        }
                        if (markingGroup.type == "solid")
                        {
                            lines.push_back(gen.get_lane_marking_line(lane, sMin, sMax, refInner, refOffset, marking.width, 0.1f));
                            colors.push_back(markingGroup.color);
                        }
                        else if (markingGroup.type == "broken")
                        {
                            int nMarkingsPast = std::floor(sMin / (BrokenGap + BrokenLength));
                            double nextMarkingBegin = nMarkingsPast * (BrokenGap + BrokenLength);
                            for (double s = nextMarkingBegin; s <= sEnd; s += BrokenGap + BrokenLength)
                            {
                                double sBeginInSegment = std::max(s, sMin);
                                double sEndInSegment = std::min(s + BrokenLength, sEnd);
                                if (sEndInSegment > sBeginInSegment + 0.1f)
                                {
                                    odr::Line3D markingLine = gen.get_lane_marking_line(lane, 
                                        sBeginInSegment, sEndInSegment, refInner, refOffset, marking.width, 0.1f);
                                    lines.push_back(markingLine);
                                    colors.push_back(markingGroup.color);
                                }
                            }
                        }

                        for (int i = 0; i != lines.size(); ++i)
                        {
                            QPolygonF markingPoly = LineToPoly(lines[i]);
                            auto markingItem = new QGraphicsPolygonItem(markingPoly, this);
                            markingItem->setZValue(1);
                            markingItem->setPen(Qt::NoPen);
                            Qt::GlobalColor color = colors[i] == "yellow" ? Qt::yellow : Qt::white;
                            markingItem->setBrush(QBrush(color, Qt::SolidPattern));
                        }
                    }
                }
            }
        }

        refLineHint = new QGraphicsPathItem(this);
        refLineHint->hide();
        UpdateRefLineHint();
    }

    void SectionGraphics::UpdateRefLineHint()
    {
        odr::Road& gen = road.lock()->generated;
        auto lineAppox = gen.ref_line.get_line(std::min(sBegin, sEnd), std::max(sBegin, sEnd), 0.1f);
        QPainterPath refLinePath;

        if (lineAppox.size() >= 2)
        {
            // Ref line
            auto initial = lineAppox[0];
            
            refLinePath.moveTo(initial[0], initial[1]);
            for (int i = 1; i < lineAppox.size(); ++i)
            {
                auto p = lineAppox[i];
                refLinePath.lineTo(p[0], p[1]);
            }
            // Arrow
            const auto& last = lineAppox.back();
            const auto& last2 = lineAppox[lineAppox.size() - 2];
            QVector2D lastDir(last[0] - last2[0], last[1] - last2[1]);
            lastDir.normalize();
            QVector2D arrowHead(last[0], last[1]);
            QVector2D arrowTail = arrowHead - lastDir * 1;
            QVector2D arrowLeftDir(-lastDir.y(), lastDir.x());
            QVector2D arrowLeft = arrowTail + arrowLeftDir * 1;
            QVector2D arrowRight = arrowTail - arrowLeftDir * 1;
            refLinePath.moveTo(arrowLeft.toPointF());
            refLinePath.lineTo(arrowHead.toPointF());
            refLinePath.lineTo(arrowRight.toPointF());
        }
        refLineHint->setPath(refLinePath);
        refLineHint->setPen(QPen(Qt::green, 0.3, Qt::SolidLine));
    }

    LaneGraphics::LaneGraphics(
        const QPolygonF& poly,
        odr::Line3D outerBorder, 
        odr::Line3D innerBorder,
        int laneID, int laneIDRev,
        std::string laneType,
        QGraphicsItem* parent) :
        QGraphicsPolygonItem(poly, parent),
        NormalColor(134, 132, 130),
        HighlightColor(189, 187, 185),
        laneID(laneID), laneIDReversed(laneIDRev),
        isMedian(laneType == "median")
    {
        setAcceptHoverEvents(true);

        setPen(Qt::NoPen);
        EnableHighlight(false);
        
        assert(outerBorder.size() == innerBorder.size());
        assert(outerBorder.size() >= 2);

        double outerCumLength = 0;
        for (int i = 0; i < outerBorder.size() - 1; ++i)
        {
            auto outerP1 = outerBorder[i];
            auto outerP2 = outerBorder[i + 1];
            auto innerP1 = innerBorder[i];
            auto innerP2 = innerBorder[i + 1];
            odr::Line3D subdivision;
            subdivision.push_back(outerP1);
            subdivision.push_back(outerP2);
            subdivision.push_back(innerP2);
            subdivision.push_back(innerP1);
            subdivisionPolys.push_back(LineToPoly(subdivision));
            subdivisionPortion.push_back(outerCumLength);
            outerCumLength += odr::euclDistance(outerP1, outerP2);
        }
        subdivisionPortion.push_back(outerCumLength);

        for (int i = 0; i != subdivisionPortion.size(); ++i)
        {
            subdivisionPortion[i] /= outerCumLength;
        }
        Stats::Instance("LaneGraphics Created")->Increment();
    }

    std::weak_ptr<Road> LaneGraphics::SnapCursor(QPointF scenePos, double& outS)
    {
        auto parentSection = dynamic_cast<RoadRunner::SectionGraphics*>(parentItem());
        double sBegin = parentSection->sBegin;
        double sEnd = parentSection->sEnd;

        QVector2D pEvent(scenePos);

        for (int i = 0; i != subdivisionPolys.size(); ++i)
        {
            const QPolygonF& subdivision = subdivisionPolys[i];
            if (subdivision.containsPoint(scenePos, Qt::FillRule::OddEvenFill))
            {
                double pMin = subdivisionPortion[i];
                double pMax = subdivisionPortion[i + 1];
                QVector2D p0(subdivision.at(0));
                QVector2D p1(subdivision.at(1));
                QVector2D p2(subdivision.at(2));
                QVector2D p3(subdivision.at(3));

                double dUp = pEvent.distanceToLine(p1, (p2 - p1).normalized());
                double dDown = pEvent.distanceToLine(p0, (p3 - p0).normalized());
                double portion = dDown / (dDown + dUp);
                portion = pMin * (1 - portion) + pMax * portion;
                double s = sBegin * (1 - portion) + sEnd * portion;
                outS = odr::clamp(s, sBegin, sEnd);

                return parentSection->road;
            }
        }

        return std::weak_ptr<RoadRunner::Road>();
    }

    std::shared_ptr<Road> LaneGraphics::GetRoad() const
    {
        auto parentRoad = dynamic_cast<RoadRunner::SectionGraphics*>(parentItem());
        return parentRoad->road.lock();
    }

    void LaneGraphics::EnableHighlight(bool enabled)
    {
        setBrush(QBrush(isMedian ? Qt::yellow : (enabled ? HighlightColor : NormalColor), Qt::SolidPattern));
    }

    int LaneGraphics::LaneID() const
    {
        auto parentSection = dynamic_cast<RoadRunner::SectionGraphics*>(parentItem());
        return parentSection->sBegin < parentSection->sEnd ? laneID : laneIDReversed;
    }
}

