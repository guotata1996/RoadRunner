#pragma once

#include "junction.h"
#include <map>
#include "spdlog/spdlog.h"

#ifdef G_TEST
#include <gtest/gtest.h>
#endif

void EnsureEndsMeet(const odr::Road& road1, double s1, int lane1,
    const odr::Road& road2, double s2, int lane2)
{
    odr::Lane l1 = road1.get_lanesection(s1).id_to_lane.at(lane1);
    odr::Lane l2 = road2.get_lanesection(s2).id_to_lane.at(lane2);
    double t1_out = l1.outer_border.get(s1);
    double t2_out = l2.outer_border.get(s2);
    odr::Vec3D p1_out = road1.get_xyz(s1, t1_out, 0);  // Note: This returns global pos. reflane.get_xy returns local pos.
    odr::Vec3D p2_out = road2.get_xyz(s2, t2_out, 0);
    
    EXPECT_LT(odr::euclDistance(p1_out, p2_out), epsilon);

    double t1_in = l1.inner_border.get(s1);
    double t2_in = l2.inner_border.get(s2);
    odr::Vec3D p1_in = road1.get_xyz(s1, t1_in, 0);
    odr::Vec3D p2_in = road2.get_xyz(s2, t2_in, 0);

    EXPECT_LT(odr::euclDistance(p1_in, p2_in), epsilon);
}

void VerifyJunction(const RoadRunner::Junction& junction, 
    const std::vector<RoadRunner::ConnectionInfo>& connectionInfo)
{
    auto allConnections = odr::get_map_values(junction.generated.id_to_connection);

    // Make sure all incoming roads' entering lanes have matching connectings
    for (auto& incomingInfo : connectionInfo)
    {
        const odr::Road& incomingRoad = incomingInfo.road->generated;
        auto link = incomingInfo.s == 0 ? incomingRoad.predecessor : incomingRoad.successor;
#ifdef G_TEST
        EXPECT_EQ(link.type, odr::RoadLink::Type_Junction);
        EXPECT_EQ(link.id, junction.generated.id);
#endif
        std::map<int, std::pair<std::string, int>> incomingLaneToConnectingRoadLane;
        for (const auto& connection : allConnections)
        {
            if (connection.incoming_road == incomingRoad.id)
            {
                EXPECT_EQ(connection.contact_point, odr::JunctionConnection::ContactPoint_Start);
                for (const auto& ll : connection.lane_links)
                {
                    incomingLaneToConnectingRoadLane.emplace(ll.from, std::make_pair(connection.connecting_road, ll.to));
                }
            }
        }

        auto enteringLanes = incomingRoad.s_to_lanesection.begin()->second.get_sorted_driving_lanes(incomingInfo.s == 0 ? 1 : -1);
        for (const odr::Lane& enteringLane : enteringLanes)
        {
            auto connectingRoadAndLane = incomingLaneToConnectingRoadLane.at(enteringLane.id);
            std::string connectingRoadID = connectingRoadAndLane.first;
            int connectingLane = connectingRoadAndLane.second;
            auto connectingRoad = std::find_if(junction.connectingRoads.begin(), junction.connectingRoads.end(), 
                [connectingRoadID](auto& road) {return road.ID() == connectingRoadID; });
            
            EnsureEndsMeet(incomingRoad, incomingInfo.s, enteringLane.id,
                connectingRoad->generated, 0, connectingLane);
        }
    }

    // Make sure all connecting roads have matching outgoing lanes
    for (auto& conneting : junction.connectingRoads)
    {
        auto outLink = conneting.generated.successor;
#ifdef G_TEST
        EXPECT_EQ(conneting.generated.junction, junction.generated.id);
        EXPECT_EQ(outLink.type, odr::RoadLink::Type_Road);
#endif
        std::string outgoingID = outLink.id;
        std::string connectingID = conneting.ID();
        auto connection = std::find_if(allConnections.begin(), allConnections.end(),
            [connectingID](auto& connection) {return connectingID == connection.connecting_road; });
        
        EXPECT_EQ(connection->outgoing_road, outgoingID);
        auto outgoingItr = std::find_if(connectionInfo.begin(), connectionInfo.end(),
            [outgoingID](const RoadRunner::ConnectionInfo& info) {return info.road->ID() == outgoingID; });
        for (const auto& ll : connection->lane_links)
        {
            EnsureEndsMeet(conneting.generated, conneting.generated.length, ll.to,
                outgoingItr->road->generated, outgoingItr->s, ll.next);
        }
    }
}