#include "OpenDriveMap.h"

#include <qgraphicsitem.h>
#include <optional>

class Vehicle
{
public:
    /*Initiate graphics*/
    Vehicle(odr::LaneKey initialLane, double initialLocalS,
        odr::LaneKey destLane, double destS, double maxV = 20);

    /*Clean graphics*/
    void Clear();

    /*Return false if fail*/
    bool Step(double dt, const odr::OpenDriveMap& map, const odr::RoutingGraph& graph, 
        const std::unordered_map<odr::LaneKey, std::map<double, std::shared_ptr<Vehicle>>>& vehiclesOnLane);

    double CurrS() const;
    std::vector<odr::LaneKey> OccupyingLanes() const; // 2 (parallel lanes) when lane switching
    odr::Vec3D TipPos() const;
    odr::Vec3D TailPos() const;

    std::shared_ptr<Vehicle> GetLeader(const odr::OpenDriveMap& map,
        const std::unordered_map<odr::LaneKey, std::map<double, std::shared_ptr<Vehicle>>>& vehiclesOnLane,
        double& outDistance, double lookforward = 50) const;

    double vFromGibbs(double dt, std::shared_ptr<Vehicle> leader, double distance) const;

    const std::string ID;

private:
    void updateNavigation(const odr::OpenDriveMap& map, const odr::RoutingGraph& routingGraph);

    double s; // inside current lane section
    double currLaneLength;
    bool firstStep;
    
    double tOffset; // non-zero when lane change starts; gradually decreases to zero
    double laneChangeDueS;

    std::vector<odr::LaneKey> navigation;
    std::optional<odr::LaneKey> lcFrom;  // active during a lane change

    const double MaxSwitchLaneDistance = 50;

    const odr::LaneKey DestLane;
    const double DestS;
    const double MaxV;

    odr::Vec3D position;
    double heading;
    double velocity;
    QGraphicsRectItem* graphics;
    QGraphicsLineItem* leaderVisual;
};