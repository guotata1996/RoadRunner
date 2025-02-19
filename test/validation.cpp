#include "validation.h"

#include "id_generator.h"
#include "world.h"
#include "road.h"
#include "junction.h"

#ifdef G_TEST
    #include <gtest/gtest.h>
#else
    #include "change_tracker.h"
#endif

#include <fstream> // CompareFiles
#include <iterator> // CompareFiles
#include <algorithm> // CompareFiles

namespace LTest
{
#ifndef G_TEST
    void Validation::ValidateMap()
    {
        RoadIDSetMatch();
        JunctionIDSetMatch();
        VerifyRoadJunctionPtr();

        // Each junction geometry okay
        for (auto idAndJunction : IDGenerator::ForType(IDType::Junction)->assignTo)
        {
            auto junc = static_cast<LM::AbstractJunction*>(idAndJunction.second);
            VerifyJunction(junc);
        }

        for (auto road : World::Instance()->allRoads)
        {
            VerifySingleRoad(road->generated);
            VerifySingleRoadGraphics(*road.get());
        }

        VerifyRoutingGraph();
    }

    // road IDs match among IDGenerator | world | odrMap
    void Validation::RoadIDSetMatch()
    {
        const auto& serializedMap = LM::ChangeTracker::Instance()->Map();
        auto world = World::Instance();

        std::set<std::string> roadIDsFromSerialized;
        std::set<std::string> nonConnRoadIDsFromSerialized;

        for (auto idAndRoad : serializedMap.id_to_road)
        {
            assert(idAndRoad.first == idAndRoad.second.id);
            roadIDsFromSerialized.insert(idAndRoad.first);
            auto roadPtr = IDGenerator::ForType(IDType::Road)->GetByID<LM::Road>(idAndRoad.first);
            assert(roadPtr != nullptr);
            if (idAndRoad.second.junction == "-1")
            {
                nonConnRoadIDsFromSerialized.insert(idAndRoad.first);
            }
        }

        std::set<std::string> nonConnRoadIDsFromWorld;
        for (const auto& road : world->allRoads)
        {
            nonConnRoadIDsFromWorld.insert(road->ID());
        }

        std::set<std::string> roadIDsFromIDGenerator;
        for (auto idAndRoad : IDGenerator::ForType(IDType::Road)->assignTo)
        {
            roadIDsFromIDGenerator.insert(std::to_string(idAndRoad.first));
        }
        assert(roadIDsFromSerialized == roadIDsFromIDGenerator);
        assert(nonConnRoadIDsFromSerialized == nonConnRoadIDsFromWorld);
    }

    // Junction IDs match between IDGenerator | odrMap
    void Validation::JunctionIDSetMatch()
    {
        const auto& serializedMap = LM::ChangeTracker::Instance()->Map();
        std::set<std::string> junctionIDsFromSerialized;
        for (auto idAndJunction : serializedMap.id_to_junction)
        {
            assert(idAndJunction.first == idAndJunction.second.id);
            junctionIDsFromSerialized.insert(idAndJunction.first);
            auto juncPtr = IDGenerator::ForType(IDType::Junction)->GetByID<LM::AbstractJunction>(idAndJunction.first);
            assert(juncPtr != nullptr);
        }

        std::set<std::string> junctionIDsFromIDGenerator;
        for (auto idAndJunction : IDGenerator::ForType(IDType::Junction)->assignTo)
        {
            junctionIDsFromIDGenerator.insert(std::to_string(idAndJunction.first));
        }
        assert(junctionIDsFromSerialized == junctionIDsFromIDGenerator);
    }

    void Validation::VerifyRoadJunctionPtr()
    {
        const auto& serializedMap = LM::ChangeTracker::Instance()->Map();
        for (const auto& idAndRoad : serializedMap.id_to_road)
        {
            auto roadPtr = IDGenerator::ForType(IDType::Road)->GetByID<LM::Road>(idAndRoad.first);
            auto serializedRoad = idAndRoad.second;
            if (serializedRoad.predecessor.type == odr::RoadLink::Type_Junction)
            {
                assert(serializedRoad.predecessor.id == roadPtr->predecessorJunction->ID());
            }
            if (serializedRoad.successor.type == odr::RoadLink::Type_Junction)
            {
                assert(serializedRoad.successor.id == roadPtr->successorJunction->ID());
            }
        }
        for (const auto& idAndJunction : serializedMap.id_to_junction)
        {
            auto junctionPtr = (IDGenerator::ForType(IDType::Junction)->GetByID<LM::AbstractJunction>(idAndJunction.first));
            auto commonPtr = dynamic_cast<LM::Junction*>(junctionPtr);

            if (commonPtr == nullptr)
            {
                // Direct junction has no connecting road
                continue;
            }
            auto serializedJunction = idAndJunction.second;
            std::set<std::string> connectingsFromSerialized;
            for (const auto& idAndConn : serializedJunction.id_to_connection)
            {
                std::string connRoad = idAndConn.second.connecting_road;
                connectingsFromSerialized.insert(connRoad);
            }

            std::set<std::string> connectingsFromPointer;
            for (auto info : commonPtr->connectingRoads)
            {
                connectingsFromPointer.insert(info->ID());
            }
            assert(connectingsFromSerialized == connectingsFromPointer);
        }
    }

    void Validation::VerifyRoutingGraph()
    {
        const auto& serializedMap = LM::ChangeTracker::Instance()->Map();
        const auto& routingGraph = serializedMap.get_routing_graph();
        for (auto& lane_successor : routingGraph.lane_key_to_successors)
        {
            auto fromLane = lane_successor.first;
            auto fromRoad = serializedMap.id_to_road.at(fromLane.road_id);
            auto fromSection = fromRoad.get_lanesection(fromLane.lanesection_s0);
            auto fromLaneEndS = fromLane.lane_id < 0 ? fromRoad.get_lanesection_length(fromSection) + fromLane.lanesection_s0 
                : fromLane.lanesection_s0;

            for (auto toLane : lane_successor.second)
            {
                auto toRoad = serializedMap.id_to_road.at(toLane.road_id);
                auto toSection = toRoad.get_lanesection(toLane.lanesection_s0);
                auto toLaneStartS = toLane.lane_id < 0 ? toLane.lanesection_s0
                : toRoad.get_lanesection_length(toSection) + toLane.lanesection_s0;

                EnsureEndsMeet(&fromRoad, fromLaneEndS, fromSection.id_to_lane.at(fromLane.lane_id),
                    &toRoad, toLaneStartS, toSection.id_to_lane.at(toLane.lane_id));
            }
        }

        for (auto& lane_predecessor : routingGraph.lane_key_to_predecessors)
        {
            auto toLane = lane_predecessor.first;
            auto toRoad = serializedMap.id_to_road.at(toLane.road_id);
            auto toSection = toRoad.get_lanesection(toLane.lanesection_s0);
            auto toLaneStartS = toLane.lane_id < 0 ? toLane.lanesection_s0
                : toRoad.get_lanesection_length(toSection) + toLane.lanesection_s0;

            for (auto fromLane : lane_predecessor.second)
            {
                auto fromRoad = serializedMap.id_to_road.at(fromLane.road_id);
                auto fromSection = fromRoad.get_lanesection(fromLane.lanesection_s0);
                auto fromLaneEndS = fromLane.lane_id < 0 ? fromLane.lanesection_s0 + fromRoad.get_lanesection_length(fromSection)
                    : fromLane.lanesection_s0;

                EnsureEndsMeet(&fromRoad, fromLaneEndS, fromSection.id_to_lane.at(fromLane.lane_id),
                    &toRoad, toLaneStartS, toSection.id_to_lane.at(toLane.lane_id));
            }
        }
    }
#endif

    bool Validation::CompareFiles(const std::string& p1, const std::string& p2)
    {
        std::ifstream f1(p1, std::ifstream::binary | std::ifstream::ate);
        std::ifstream f2(p2, std::ifstream::binary | std::ifstream::ate);

        if (f1.fail() || f2.fail()) {
            return false; //file problem
        }

        if (f1.tellg() != f2.tellg()) {
            return false; //size mismatch
        }

        //seek back to beginning and use std::equal to compare contents
        f1.seekg(0, std::ifstream::beg);
        f2.seekg(0, std::ifstream::beg);
        return std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
            std::istreambuf_iterator<char>(),
            std::istreambuf_iterator<char>(f2.rdbuf()));
    }
}