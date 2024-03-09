#pragma once
#include "OpenDriveMap.h"

namespace RoadRunner
{
    constexpr double LaneWidth = 3.25;

    typedef int8_t type_t;
    typedef uint32_t type_s;

    double to_odr_unit(type_s l);
    double to_odr_unit(type_t l);
    type_s from_odr_unit(double l);

    struct SectionProfile
    {
        type_t offsetx2; // follows XODR s definition
        type_t laneCount; // non-negative value

        bool operator == (const SectionProfile& another) const
        {
            return offsetx2 == another.offsetx2 && laneCount == another.laneCount;
        }

        bool operator != (const SectionProfile& another) const
        {
            return offsetx2 != another.offsetx2 || laneCount != another.laneCount;
        }
    };

    class RoadProfile
    {
    public:
        RoadProfile(uint8_t nLanes_Left, int8_t offsetX2_Left, uint8_t nLanes_Right, int8_t offsetX2_Right);

        RoadProfile& operator=(const RoadProfile& other);

        void OverwriteSection(int side, double start, double end, uint8_t nLanes, int8_t offsetX2);
        void OverwriteSection(int side, type_s start, type_s end, uint8_t nLanes, int8_t offsetX2);

        SectionProfile LeftEntrance() const;
        SectionProfile LeftExit() const;
        SectionProfile RightEntrance() const;
        SectionProfile RightExit() const;

        // right side keys are in (s_small, s_big)
        // left side keys are in (s_big, s_small)
        // begins with 0, ends at length
        std::map<std::pair<type_s, type_s>, SectionProfile> GetAllSections(type_s length, int side) const;

        bool HasSide(int side);

        void Apply(double length, odr::Road&) const;

    protected:
        void ConvertSide(bool rightSide,
            std::string roadID,
            type_s length,
            std::map<double, odr::LaneSection>& laneSectionResult,
            std::map<double, odr::Poly3>& laneOffsetResult) const;

        void RemoveRedundantProfileKeys(int side);

        std::map<double, odr::Poly3> _MakeTransition(
            type_s start_s, type_s end_s,
            type_t start_t2, type_t end_t2, bool rightSide, type_s length) const;

        std::map<double, odr::Poly3> _MakeStraight(type_s start_s, type_s end_s, type_t const_t,
            bool rightSide, type_s length) const;

        // Convert difference in L/R lane offset into median center lane
        std::map<double, odr::Poly3> _ComputeMedian(
            const std::map<double, odr::Poly3>& leftOffsets,
            const std::map<double, odr::Poly3> rightOffsets,
            type_s length) const;

        void _MergeSides(odr::Road& rtn,
            const std::map<double, odr::LaneSection>& leftSections,
            const std::map<double, odr::Poly3>& centerWidths,
            const std::map<double, odr::LaneSection>& rightSections,
            type_s length) const;

        const type_s MaxTransitionS = 20 * 100;

        std::map<type_s, SectionProfile> leftProfiles, rightProfiles;

        struct TransitionInfo
        {
            type_s cumulativeS;  // front start (right:s=0, left: s=L)
            type_t oldCenter2, newCenter2; // right: positive
            int startLanes, newLanesOnLeft, newLanesOnRight;
            type_s transitionHalfLength;
        };
    };
}