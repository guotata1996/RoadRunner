#include "junction.h"
#include <sstream>

#include "id_generator.h"
#include "constants.h"
#include "world.h"

#ifndef G_TEST
    #include "road_graphics.h"
#endif

namespace RoadRunner
{
    AbstractJunction::AbstractJunction() :
        generated("", IDGenerator::ForJunction()->GenerateID(this), odr::JunctionType::Common)
    {
        generated.name = "Junction " + generated.id;
    }

    AbstractJunction::AbstractJunction(const odr::Junction& serialized) :
        generated(serialized)
    {
        IDGenerator::ForJunction()->TakeID(ID(), this);
    }

    int AbstractJunction::Attach(ConnectionInfo conn)
    {
        if (formedFrom.find(conn) != formedFrom.end())
        {
            generationError = Junction_DuplicateConn;
            return generationError;
        }
        auto connRoad = conn.road.lock();
        double connS = conn.contact == odr::RoadLink::ContactPoint_Start ?
            0 : connRoad->Length();
        RoadRunner::CubicSplineGenerator::OverwriteSection(
            connRoad->RefLine().elevation_profile, connRoad->Length(), connS, connS, Elevation());
#ifndef G_TEST
        connRoad->GenerateOrUpdateSectionGraphicsBetween(
            std::max(connS - RoadRunner::CubicSplineGenerator::MaxTransitionLength, 0.0),
            std::min(connS + RoadRunner::CubicSplineGenerator::MaxTransitionLength, connRoad->Length()));
#endif

        std::vector<ConnectionInfo> newConnections = { conn };
        for (auto existing : formedFrom)
        {
            // if (existing.road.expired())
            //    continue;
            ConnectionInfo existingInfo{ existing.road.lock(), existing.contact, existing.skipProviderLanes };
            newConnections.push_back(existingInfo);
        }
        formedFrom.clear();
        return CreateFrom(newConnections);
    }

    void AbstractJunction::NotifyPotentialChange()
    {
        NotifyPotentialChange(ChangeInConnecting());
    }

    void AbstractJunction::NotifyPotentialChange(const ChangeInConnecting& detail)
    {
        std::shared_ptr<Road> subject;
        if (detail._type != detail.Type_Others)
        {
            subject = detail.subject.lock();
        }

        std::vector<ConnectionInfo> updatedInfoList;
        bool needReGen{ false };
        for (const auto record : formedFrom)
        {
            auto recordedRoad = record.road.lock();

            if (recordedRoad == nullptr)
            {
                needReGen = true;
            }
            else if (detail._type == detail.Type_Reverse && subject == recordedRoad)
            {
                needReGen = true;
                auto newContact = record.contact == odr::RoadLink::ContactPoint_Start ?
                    odr::RoadLink::ContactPoint_End : odr::RoadLink::ContactPoint_Start;
                auto updatedInfo = ConnectionInfo(recordedRoad, newContact, record.skipProviderLanes);
                updatedInfoList.push_back(updatedInfo);
            }
            else if (detail._type == detail.Type_DetachAtEnd_Temp && record.contact == odr::RoadLink::ContactPoint_End &&
                subject == recordedRoad)
            {
                subject->generated.successor = odr::RoadLink();
                subject->successorJunction.reset();
            }
            else
            {
                // Must recalculate profile,pos,hdg to see if update is needed
                auto updatedInfo = ConnectionInfo(recordedRoad, record.contact, record.skipProviderLanes);
                updatedInfoList.push_back(updatedInfo);
                if (updatedInfo != record)
                    needReGen = true;
            }
        }

        if (detail._type == detail.Type_DetachAtEnd_Temp)
        {
            formedFrom.clear();
            formedFrom.insert(updatedInfoList.cbegin(), updatedInfoList.cend());
        }
        else if (needReGen && updatedInfoList.size() > 1)
        {
            spdlog::trace("Junction {} regen from {} roads", ID(), updatedInfoList.size());
            formedFrom.clear();
            CreateFrom(updatedInfoList);
        }
        else if (updatedInfoList.size() == 1)
        {
            auto onlyRoad = updatedInfoList.begin()->road.lock();
            if (updatedInfoList.begin()->contact == odr::RoadLink::ContactPoint_Start)
            {
                onlyRoad->predecessorJunction.reset();
            }
            else if (updatedInfoList.begin()->contact == odr::RoadLink::ContactPoint_End)
            {
                onlyRoad->successorJunction.reset();
            }
            else
            {
                assert(false);
            }

            clearLinkage(ID(), onlyRoad->ID());
            IDGenerator::ForRoad()->NotifyChange(onlyRoad->ID());
            formedFrom.clear();
            // Junction will then be destroyed
        }
    }

    void AbstractJunction::AttachNoRegenerate(ConnectionInfo conn)
    {
        formedFrom.insert(conn);
        if (conn.contact == odr::RoadLink::ContactPoint_Start)
        {
            conn.road.lock()->predecessorJunction = shared_from_this();
        }
        else
        {
            conn.road.lock()->successorJunction = shared_from_this();
        }
    }

    void AbstractJunction::DetachNoRegenerate(std::shared_ptr<Road> road)
    {
        auto myPtr = shared_from_this();
        if (road->successorJunction == myPtr)
        {
            formedFrom.erase(RoadRunner::ConnectionInfo(road, odr::RoadLink::ContactPoint_End));
            road->successorJunction.reset();
        }

        if (road->predecessorJunction == myPtr)
        {
            formedFrom.erase(RoadRunner::ConnectionInfo(road, odr::RoadLink::ContactPoint_Start));
            road->predecessorJunction.reset();
        }
    }

    bool AbstractJunction::CanDegerate() const
    {
        if (formedFrom.size() != 2)
        {
            return false;
        }
        auto roadA = formedFrom.begin()->road.lock();
        auto contactA = formedFrom.begin()->contact;
        auto roadB = formedFrom.rbegin()->road.lock();
        auto contactB = formedFrom.rbegin()->contact;
        if (roadA == roadB)
        {
            // RoadJoin_SelfLoop
            return false;
        }
        return true;
    }

    void AbstractJunction::Degenerate()
    {
        auto roadA = formedFrom.begin()->road.lock();
        auto contactA = formedFrom.begin()->contact;
        auto roadB = formedFrom.rbegin()->road.lock();
        auto contactB = formedFrom.rbegin()->contact;

        clearLinkage(ID(), roadA->ID());
        if (contactA == odr::RoadLink::ContactPoint_Start)
        {
            roadA->predecessorJunction.reset();
        }
        else
        {
            roadA->successorJunction.reset();
        }
        if (contactB == odr::RoadLink::ContactPoint_Start)
        {
            roadB->predecessorJunction.reset();
        }
        else
        {
            roadB->successorJunction.reset();
        }
        World::Instance()->allRoads.erase(roadB);
        auto joinResult = Road::JoinRoads(roadA, contactA, roadB, contactB);
        if (joinResult != RoadJoin_Success)
        {
            throw std::logic_error("AbstractJunction::Degenerate join exception");
        }
        // Destruct self
        formedFrom.clear();
    }

    std::set<std::pair<Road*, odr::RoadLink::ContactPoint>> AbstractJunction::GetConnected() const
    {
        std::set<std::pair<Road*, odr::RoadLink::ContactPoint>> rtn;
        for (auto info : formedFrom)
        {
            rtn.emplace(std::make_pair(info.road.lock().get(), info.contact));
        }
        return rtn;
    }

    std::string AbstractJunction::Log() const
    {
        std::stringstream ss;
        ss << "Junction " << ID() << "\n";
        for (auto contact : formedFrom)
        {
            auto contactStr = contact.contact == odr::RoadLink::ContactPoint_Start ? "Start" :
                contact.contact == odr::RoadLink::ContactPoint_End ? "End" : "None";
            ss << "    " << contact.road.lock()->ID() << " connected at " << contactStr << "\n";
        }
        return ss.str();
    }

    void AbstractJunction::FillConnectionInfo(ConnectionInfo& info) const
    {
        auto record = formedFrom.find(info);
        info = *record;
    }

    double AbstractJunction::Elevation() const
    {
        const auto road = formedFrom.begin()->road.lock();
        auto s = formedFrom.begin()->contact == odr::RoadLink::ContactPoint_Start ? 0 :
            road->Length();
        return road->RefLine().elevation_profile.get(s);
    }

    AbstractJunction::~AbstractJunction()
    {
        for (const auto& connectingRoad : formedFrom)
        {
            if (!connectingRoad.road.expired())
            {
                spdlog::error("Junction gets destroyed before its connected road!");
            }
        }

        if (!ID().empty())
        {
            IDGenerator::ForJunction()->FreeID(ID());
        }
    }

    Junction::Junction() : AbstractJunction() {}

    Junction::Junction(const odr::Junction& serialized) : AbstractJunction(serialized)
    {
        // Link connecting road
        for (const auto& id2Connection : generated.id_to_connection)
        {
            auto connectingRoadID = id2Connection.second.connecting_road;
            auto roadPtr = static_cast<RoadRunner::Road*>(IDGenerator::ForRoad()->GetByID(connectingRoadID));
            connectingRoads.push_back(roadPtr->shared_from_this());
        }
    }

    int Junction::CreateFrom(const std::vector<ConnectionInfo>& connected)
    {
        connectingRoads.clear();

        generationError = GenerateConnections(generated.id, connected, connectingRoads);

        std::for_each(connected.begin(), connected.end(), [this](const ConnectionInfo& info) {
            formedFrom.insert(info);
            auto roadPtr = info.road.lock();
            if (info.contact == odr::RoadLink::ContactPoint_Start)
            {
                roadPtr->predecessorJunction = shared_from_this();
            }
            else
            {
                roadPtr->successorJunction = shared_from_this();
            }
        });

        generated.id_to_connection.clear();
        int junctionConnID = 0;
        for (auto& connecting : connectingRoads)
        {
            auto incomingRoad = connecting->generated.predecessor.id;

            odr::JunctionConnection prevConn(std::to_string(junctionConnID++),
                incomingRoad, connecting->ID(),
                odr::JunctionConnection::ContactPoint_Start);

            for (odr::Lane connectinglane : connecting->generated.s_to_lanesection.rbegin()->second.get_sorted_driving_lanes(-1))
            {
                prevConn.lane_links.insert(odr::JunctionLaneLink(connectinglane.predecessor, connectinglane.id));
            }
            generated.id_to_connection.emplace(prevConn.id, prevConn);

            CubicSplineGenerator::OverwriteSection(connecting->RefLine().elevation_profile,
                connecting->Length(), 0, connecting->Length(), Elevation());
        }

#ifndef G_TEST
        for (auto& connecting : connectingRoads)
        {
            if (connecting->Length() < RoadRunner::SingleDrawMaxLength)
            {
                connecting->GenerateAllSectionGraphics();
            }
            else
            {
                spdlog::warn("Connecting road length is abnormal!");
                generationError |= Junction_ConnectionInvalidShape;
            }
        }

        GenerateGraphics();
#endif
        GenerateSignalPhase();

        IDGenerator::ForJunction()->NotifyChange(ID());

        return generationError;
    }

    bool Junction::CanDegerate() const
    {
        if (!AbstractJunction::CanDegerate())
        {
            return false;
        }

        auto roadA = formedFrom.begin()->road.lock();
        auto contactA = formedFrom.begin()->contact;
        auto roadB = formedFrom.rbegin()->road.lock();
        auto contactB = formedFrom.rbegin()->contact;
        auto roadAIn = contactA == odr::RoadLink::ContactPoint_Start ? roadA->generated.rr_profile.LeftExit().laneCount :
            roadA->generated.rr_profile.RightExit().laneCount;
        auto roadAOut = contactA == odr::RoadLink::ContactPoint_Start ? roadA->generated.rr_profile.RightEntrance().laneCount :
            roadA->generated.rr_profile.LeftEntrance().laneCount;

        auto roadBIn = contactB == odr::RoadLink::ContactPoint_Start ? roadB->generated.rr_profile.LeftExit().laneCount :
            roadB->generated.rr_profile.RightExit().laneCount;
        auto roadBOut = contactB == odr::RoadLink::ContactPoint_Start ? roadB->generated.rr_profile.RightEntrance().laneCount :
            roadB->generated.rr_profile.LeftEntrance().laneCount;

        if (roadAIn > 0 != roadBOut > 0 || roadAOut > 0 != roadBIn > 0)
        {
            // RoadJoin_DirNoOutlet
            // RoadRunnerTODO: limit can be loosen
            return false;
        }
        return true;
    }

#ifndef G_TEST
    void Junction::GenerateGraphics()
    {
        auto boundary = CalcBoundary();
        junctionGraphics = std::make_unique<JunctionGraphics>(boundary);
        junctionGraphics->setZValue(Elevation() + 0.01);
    }
#endif

    uint8_t Junction::GetTurningSemanticsForIncoming(std::string incomingRoad, int incomingLane) const
    {
        uint8_t rtn = 0;
        for (auto id_conn : generated.id_to_connection)
        {
            if (id_conn.second.incoming_road == incomingRoad)
            {
                for (auto ll : id_conn.second.lane_links)
                {
                    if (ll.from == incomingLane)
                    {
                        auto connRoadID = id_conn.second.connecting_road;
                        auto connectingRoad = static_cast<RoadRunner::Road*>(IDGenerator::ForRoad()->GetByID(connRoadID));
                        auto startGrad = connectingRoad->generated.ref_line.get_grad_xy(0);
                        auto endGrad = connectingRoad->generated.ref_line.get_grad_xy(connectingRoad->Length());
                        auto turnAngle = odr::angle(startGrad, endGrad);
                        if (std::abs(turnAngle) > M_PI - 0.1)
                        {
                            rtn |= TurningSemantics::Turn_U;
                        }
                        else if (turnAngle > M_PI_4)
                        {
                            rtn |= TurningSemantics::Turn_Left;
                        }
                        else if (turnAngle < -M_PI_4)
                        {
                            rtn |= TurningSemantics::Turn_Right;
                        }
                        else
                        {
                            rtn |= TurningSemantics::Turn_No;
                        }
                    }
                }
            }
        }
        return rtn;
    }

    void Junction::GenerateSignalPhase()
    {
        std::map<std::pair<std::string, std::string>, bool> conflictResultBuffer;
        std::vector<std::shared_ptr<Road>> pendingAssign;

        for (auto road : connectingRoads)
        {
            pendingAssign.push_back(road);
        }

        std::sort(pendingAssign.begin(), pendingAssign.end(),
            [](const std::shared_ptr<Road>& roadA, const std::shared_ptr<Road>& roadB)
            {
                int aLanes = roadA->generated.s_to_lanesection.begin()->second.get_sorted_driving_lanes(-1).size();
                int bLanes = roadB->generated.s_to_lanesection.begin()->second.get_sorted_driving_lanes(-1).size();
                return aLanes < bLanes;
            });
        
        std::map<std::string, std::set<std::pair<std::string, int>>> connectingToIncomingLanes;
        for (const auto& id_conn : generated.id_to_connection)
        {
            const auto& conn = id_conn.second;
            for (const auto& lane_links : conn.lane_links)
            {
                auto key = std::make_pair(conn.incoming_road, lane_links.from);
                connectingToIncomingLanes[conn.connecting_road].emplace(key);
            }
        }

        std::vector<std::vector<std::shared_ptr<Road>>> nonOverlapGroups;
        while (!pendingAssign.empty())
        {
            auto groupInitiator = pendingAssign.back();
            pendingAssign.pop_back();
            std::vector<std::shared_ptr<Road>> group = { groupInitiator };
            
            // If two connecting roads originate from same lane, put them in the same group.
            auto allIncomings = connectingToIncomingLanes.at(groupInitiator->ID());
            while (true)
            {
                bool enrolledThisRound = false;
                for (int i = pendingAssign.size() - 1; i >= 0; --i)
                {
                    auto candidate = pendingAssign[i];
                    const auto& candidateIncoming = connectingToIncomingLanes.at(candidate->ID());
                    std::vector<std::pair<std::string, int>> intersectionResult;
                    std::set_intersection(candidateIncoming.begin(), candidateIncoming.end(),
                        allIncomings.begin(), allIncomings.end(), std::back_inserter(intersectionResult));
                    if (!intersectionResult.empty())
                    {
                        allIncomings.insert(candidateIncoming.begin(), candidateIncoming.end());
                        group.push_back(candidate);
                        pendingAssign.erase(pendingAssign.begin() + i);
                        enrolledThisRound = true;
                    }
                }
                if (!enrolledThisRound)
                    break;
            }

            for (int i = pendingAssign.size() - 1; i >= 0; --i)
            {
                auto candidate = pendingAssign[i];
                // Accept only if there's no conflict with any member in group
                bool hasConflict = false;
                for (auto existingMember: group)
                {
                    if (connRoadsConflictBuffered(candidate->generated, existingMember->generated, conflictResultBuffer))
                    {
                        hasConflict = true;
                        break;
                    }
                }
                if (!hasConflict)
                {
                    group.push_back(candidate);
                    pendingAssign.erase(pendingAssign.begin() + i);
                }
            }
            nonOverlapGroups.push_back(group);
        }

        // expand raw group to include non-conflicting members from other groups
        std::vector<std::vector<std::shared_ptr<Road>>> expandedGroup(nonOverlapGroups.size());
        for (int i = 0; i != nonOverlapGroups.size(); ++i)
        {
            expandedGroup[i] = nonOverlapGroups[i];
            for (int j = 0; j != nonOverlapGroups.size(); ++j)
            {
                if (i == j) continue;
                for (auto candidate : nonOverlapGroups[j])
                {
                    bool hasConflict = false;
                    for (auto existingMember : expandedGroup[i])
                    {
                        if (connRoadsConflictBuffered(candidate->generated, existingMember->generated, conflictResultBuffer))
                        {
                            hasConflict = true;
                            break;
                        }
                    }
                    if (!hasConflict)
                    {
                        expandedGroup[i].push_back(candidate);
                    }
                }
            }
        }

        // Write to xodr
        std::map<std::string, std::set<int>> connectingToSignalPhase;
        for (int phase = 0; phase != expandedGroup.size(); ++phase)
        {
            for (auto member : expandedGroup[phase])
            {
                connectingToSignalPhase[member->ID()].emplace(phase);
            }
        }

        for (auto& id_conn : generated.id_to_connection)
        {
            id_conn.second.signalPhases = connectingToSignalPhase.at(id_conn.second.connecting_road);
        }
    }

    DirectJunction::DirectJunction(ConnectionInfo aInterfaceProvider) : AbstractJunction()
    {
        formedFrom.insert(aInterfaceProvider);
        generated.type = odr::JunctionType::Direct;

        interfaceDir = calcInterfaceDir(aInterfaceProvider);
    }

    DirectJunction::DirectJunction(const odr::Junction& serialized) : AbstractJunction(serialized)
    {
        generated.type = odr::JunctionType::Direct;
        auto interfaceProviderID = serialized.id_to_connection.cbegin()->second.incoming_road;
        
        auto interfaceProvider = static_cast<RoadRunner::Road*>(IDGenerator::ForRoad()->GetByID(interfaceProviderID));
        odr::RoadLink::ContactPoint interfaceContact;
        if (interfaceProvider->generated.predecessor.type == odr::RoadLink::Type_Junction &&
            interfaceProvider->generated.predecessor.id == ID())
        {
            interfaceContact = odr::RoadLink::ContactPoint_Start;
        }
        else if (interfaceProvider->generated.successor.type == odr::RoadLink::Type_Junction &&
            interfaceProvider->generated.successor.id == ID())
        {
            interfaceContact = odr::RoadLink::ContactPoint_End;
        }
        else throw;

        interfaceDir = calcInterfaceDir(ConnectionInfo(interfaceProvider->shared_from_this(), interfaceContact));
    }

    int DirectJunction::CreateFrom(const std::vector<ConnectionInfo>& connectedInfo)
    {
        formedFrom.insert(connectedInfo.begin(), connectedInfo.end());

        if (formedFrom.size() != connectedInfo.size())
        {
            spdlog::error("A road cannot appear in the same direct junction twice!");
            return JunctionError::Junction_DuplicateConn;
        }

        auto interfaceInfoOrInvalid = InterfaceProvider();
        if (!interfaceInfoOrInvalid.has_value())
        {
            return JunctionError::Junction_DirectNoProvider;
        }
        auto interfaceInfo = interfaceInfoOrInvalid.value();
        std::shared_ptr<Road> interfaceProviderRoad = interfaceInfo.road.lock();
        auto interfaceContact = interfaceInfo.contact;

        if (interfaceProviderRoad == nullptr)
        {
            // Direct junction no longer holds without interface provider
            for (auto contactInfo : connectedInfo)
            {
                auto connectedRoad = contactInfo.road.lock();
                if (contactInfo.contact == odr::RoadLink::ContactPoint_Start)
                {
                    connectedRoad->predecessorJunction.reset();
                }
                else if (contactInfo.contact == odr::RoadLink::ContactPoint_End)
                {
                    connectedRoad->successorJunction.reset();
                }
                else
                {
                    throw std::logic_error("DirectJunction::CreateFrom Invalid contact point");
                }
                clearLinkage(ID(), connectedRoad->ID());
                IDGenerator::ForRoad()->NotifyChange(connectedRoad->ID());
            }
            formedFrom.clear();
            // Junction will then be destroyed
            return JunctionError::Junction_NoError;
        }

        for (const ConnectionInfo& info: connectedInfo)
        {
            auto connectedRoad = info.road.lock();
            if (info.contact == odr::RoadLink::ContactPoint_Start)
            {
                connectedRoad->predecessorJunction = shared_from_this();
                connectedRoad->generated.predecessor = odr::RoadLink(ID(), odr::RoadLink::Type_Junction);
            }
            else
            {
                connectedRoad->successorJunction = shared_from_this();
                connectedRoad->generated.successor = odr::RoadLink(ID(), odr::RoadLink::Type_Junction);
            }
            IDGenerator::ForRoad()->NotifyChange(connectedRoad->ID());
        }

        generated.id_to_connection.clear();
        int junctionConnID = 0;
        const auto& sectionProvider = interfaceProviderRoad->generated.get_lanesection(
            interfaceContact == odr::RoadLink::ContactPoint_Start ? 0 : interfaceProviderRoad->Length());
        for (auto rampInfo: connectedInfo)
        {
            auto linkedRoad = rampInfo.road.lock();
            auto linkedContact = rampInfo.contact;
            if (rampInfo == interfaceInfo)
            {
                continue;
            }
            // Here we just let incomingRoad be interface provider; while linkedRoad be narrower ramp
            odr::JunctionConnection conn(std::to_string(junctionConnID++), interfaceProviderRoad->ID(), linkedRoad->ID(),
                linkedContact == odr::RoadLink::ContactPoint_Start ? odr::JunctionConnection::ContactPoint_Start : odr::JunctionConnection::ContactPoint_End,
                interfaceContact == odr::RoadLink::ContactPoint_Start ? odr::JunctionConnection::ContactPoint_Start : odr::JunctionConnection::ContactPoint_End);

            const auto& sectionLinked = linkedRoad->generated.get_lanesection(linkedContact == odr::RoadLink::ContactPoint_Start ? 0 : linkedRoad->Length());
            
            // Link lanes from Provider to Linked (split)
            if (linkedRoad->generated.rr_profile.HasSide(1) ||      // bi-dir
                linkedContact == odr::RoadLink::ContactPoint_Start) // or single-dir lane split
            {
                auto lanesOnProvider = sectionProvider.get_sorted_driving_lanes(interfaceContact == odr::RoadLink::ContactPoint_End ? -1 : 1);
                auto lanesOnLinked = sectionLinked.get_sorted_driving_lanes(linkedContact == odr::RoadLink::ContactPoint_Start ? -1 : 1);

                for (int i = 0; i != lanesOnLinked.size(); ++i)
                {
                    conn.lane_links.insert(odr::JunctionLaneLink(lanesOnProvider[i + rampInfo.skipProviderLanes].id, lanesOnLinked[i].id));
                }
            }
            
            // Link lanes from Linked to Provider (merge)
            if (linkedRoad->generated.rr_profile.HasSide(1) ||      // bi-dir
                linkedContact == odr::RoadLink::ContactPoint_End)   // or single-dir lane merge
            {
                auto lanesOnLinked = sectionLinked.get_sorted_driving_lanes(linkedContact == odr::RoadLink::ContactPoint_End ? -1 : 1);
                auto lanesOnProvider = sectionProvider.get_sorted_driving_lanes(interfaceContact == odr::RoadLink::ContactPoint_Start ? -1 : 1);
                for (int i = 0; i != lanesOnLinked.size(); ++i)
                {
                    conn.lane_links.insert(odr::JunctionLaneLink(lanesOnProvider[i + rampInfo.skipProviderLanes].id, lanesOnLinked[i].id));
                }
            }

            generated.id_to_connection.emplace(conn.id, conn);
        }
#ifndef G_TEST
        GenerateGraphics();
#endif

        IDGenerator::ForJunction()->NotifyChange(ID());

        return JunctionError::Junction_NoError;
    }

    void DirectJunction::AttachNoRegenerate(ConnectionInfo conn)
    {
        auto road = conn.road.lock();
        auto interfaceProvider = static_cast<Road*>(IDGenerator::ForRoad()->GetByID(generated.id_to_connection.begin()->second.incoming_road));
        bool isInterfaceProvider = interfaceProvider->ID() == conn.road.lock()->ID();
        bool connIsSide = road->generated.rr_profile.HasSide(-1) && road->generated.rr_profile.HasSide(1);
        if (!isInterfaceProvider && 
            !connIsSide)
        {
            // Recover skipProviderLanes from laneLink info
            double sectionS;
            if (interfaceProvider->generated.successor.id == ID() && interfaceProvider->generated.successor.type == odr::RoadLink::Type_Junction)
            {
                sectionS = interfaceProvider->Length();
            }
            else if (interfaceProvider->generated.predecessor.id == ID() && interfaceProvider->generated.predecessor.type == odr::RoadLink::Type_Junction)
            {
                sectionS = 0;
            }
            else
            {
                throw;
            }
            auto touchingSection = interfaceProvider->generated.get_lanesection(sectionS);

            bool recovered = false;
            for (auto idAndConn : generated.id_to_connection)
            {
                if (idAndConn.second.connecting_road == conn.road.lock()->ID())
                {
                    int innerMostLinkedABS = 255;
                    for (auto laneLink : idAndConn.second.lane_links)
                    {
                        innerMostLinkedABS = std::min(innerMostLinkedABS, std::abs(laneLink.from));
                    }

                    int interfaceProvideSide = idAndConn.second.lane_links.begin()->from < 0 ? -1 : 1;
                    int innerMostLane = touchingSection.get_sorted_driving_lanes(interfaceProvideSide).begin()->id;
                    conn.skipProviderLanes = std::abs(innerMostLinkedABS - std::abs(innerMostLane));
                    recovered = true;
                    break;
                }
            }
            assert(recovered);
        }

        AbstractJunction::AttachNoRegenerate(conn);
    }

    bool DirectJunction::CanDegerate() const
    {
        if (!AbstractJunction::CanDegerate())
        {
            return false;
        }

        auto roadA = formedFrom.begin()->road.lock();
        auto contactA = formedFrom.begin()->contact;
        auto roadB = formedFrom.rbegin()->road.lock();
        auto contactB = formedFrom.rbegin()->contact;
        
        auto roadAIn = contactA == odr::RoadLink::ContactPoint_Start ? roadA->generated.rr_profile.LeftExit().laneCount :
            roadA->generated.rr_profile.RightExit().laneCount;
        auto roadAOut = contactA == odr::RoadLink::ContactPoint_Start ? roadA->generated.rr_profile.RightEntrance().laneCount :
            roadA->generated.rr_profile.LeftEntrance().laneCount;

        auto roadBIn = contactB == odr::RoadLink::ContactPoint_Start ? roadB->generated.rr_profile.LeftExit().laneCount :
            roadB->generated.rr_profile.RightExit().laneCount;
        auto roadBOut = contactB == odr::RoadLink::ContactPoint_Start ? roadB->generated.rr_profile.RightEntrance().laneCount :
            roadB->generated.rr_profile.LeftEntrance().laneCount;

        if (roadAIn != roadBOut || roadAOut != roadBIn)
        {
            return false;
        }

        return true;
    }
    uint8_t DirectJunction::GetTurningSemanticsForIncoming(std::string incomingRoad, int incomingLane) const
    {
        uint8_t rtn = RoadRunner::DeadEnd;
        for (auto id_conn : generated.id_to_connection)
        {
            if (id_conn.second.incoming_road == incomingRoad)
            {
                for (auto ll : id_conn.second.lane_links)
                {
                    if (ll.from == incomingLane)
                    {
                        rtn = 0;
                    }
                }
            }
            if (id_conn.second.connecting_road == incomingRoad)
            {
                for (auto ll : id_conn.second.lane_links)
                {
                    if (ll.to == incomingLane)
                    {
                        rtn = 0;
                    }
                }
            }
        }
        return rtn;
    }

#ifndef G_TEST
    void DirectJunction::GenerateGraphics()
    {
        junctionGraphics = std::make_unique<JunctionGraphics>(CalcCavity());
        junctionGraphics->setZValue(Elevation());

        for (auto info : formedFrom)
        {
            const auto& linkedRoad = info.road.lock();
            const auto& lookupTable = linkedRoad->generated.boundaryHide;

            for (odr::RoadLink::ContactPoint contact :
                {odr::RoadLink::ContactPoint_Start, odr::RoadLink::ContactPoint_End})
            {
                for (int side : {-1, 1})
                {
                    auto keyLookup = std::make_pair(contact, side);
                    double overlapLength = 0;
                    if (lookupTable.find(keyLookup) != lookupTable.end())
                    {
                        overlapLength = contact == odr::RoadLink::ContactPoint_Start ? lookupTable.at(keyLookup) :
                            std::abs(linkedRoad->Length() - lookupTable.at(keyLookup));
                    }
                    if (lookupTable.find(keyLookup) != lookupTable.end() && std::abs(overlapLength) > epsilon)
                    {
                        // Write hide boundary length to overlap zone
                        for (auto& id_conn : generated.id_to_connection)
                        {
                            if (id_conn.second.connecting_road == linkedRoad->ID()
                                    && id_conn.second.contact_point == contact)
                            {
                                std::set<int> lanesInvolved;
                                
                                for (auto it = id_conn.second.lane_links.begin(); it != id_conn.second.lane_links.end(); ++it)
                                {
                                    lanesInvolved.emplace(it->to);
                                }
                                int overlapLaneIt = side < 0 ? *lanesInvolved.begin() : *lanesInvolved.rbegin();

                                // update lane_link xml
                                decltype(id_conn.second.lane_links) updatedLaneLink;
                                for (auto ll : id_conn.second.lane_links)
                                {
                                    if (ll.to == overlapLaneIt)
                                    {
                                        updatedLaneLink.emplace(odr::JunctionLaneLink(ll.from, ll.to, overlapLength));
                                    }
                                    else
                                    {
                                        updatedLaneLink.emplace(odr::JunctionLaneLink(ll.from, ll.to, ll.overlapZone));
                                    }
                                }
                                id_conn.second.lane_links = updatedLaneLink;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
#endif

    odr::Vec2D DirectJunction::calcInterfaceDir(const ConnectionInfo& aInterfaceProvider)
    {
        odr::Vec2D rtn;
        auto interfaceRoad = aInterfaceProvider.road.lock();
        if (aInterfaceProvider.contact == odr::RoadLink::ContactPoint_Start)
        {
            rtn = interfaceRoad->RefLine().get_grad_xy(0);
        }
        else
        {
            rtn = interfaceRoad->RefLine().get_grad_xy(interfaceRoad->Length());
            rtn = odr::negate(rtn);
        }
        rtn = odr::normalize(rtn);
        return rtn;
    }

    std::optional<ConnectionInfo> DirectJunction::InterfaceProvider() const
    {
        std::shared_ptr<Road> interfaceProviderRoad;
        odr::RoadLink::ContactPoint interfaceContact = odr::RoadLink::ContactPoint_None;

        bool matchFound = false;
        for (const auto& connInfo : formedFrom)
        {
            auto connDir = calcInterfaceDir(connInfo);
            if (odr::dot(interfaceDir, connDir) > 0.9)
            {
                interfaceProviderRoad = connInfo.road.lock();
                interfaceContact = connInfo.contact;
                if (matchFound)
                {
                    spdlog::error("More than one road match interface direction!");
                    return std::optional<ConnectionInfo>();
                }
                matchFound = true;
            }
        }
        return ConnectionInfo(interfaceProviderRoad, interfaceContact);
    }

    std::string DirectJunction::Log() const
    {
        std::stringstream ss;
        ss << "Direct Junction " << ID() << "\n";
        auto providerOrBad = InterfaceProvider();
        if (!providerOrBad.has_value())
        {
            ss << "     Error: Invalid Interface provider!\n";
        }
        else
        {
            for (auto contact : formedFrom)
            {
                auto contactStr = contact.contact == odr::RoadLink::ContactPoint_Start ? "Start" :
                    contact.contact == odr::RoadLink::ContactPoint_End ? "End" : "None";
                auto typeStr = contact == providerOrBad.value() ? "Interface " : "Linked ";
                ss << "    " << typeStr << contact.road.lock()->ID() << " connected at " << contactStr << "\n";
            }
        }
        return ss.str();
    }
} // namespace RoadRunner