#pragma once

#include "OpenDriveMap.h"
#include <boost/optional.hpp>

#include <string>
#include <stack>
#include <vector>
namespace LTest { class Validation; }
class VehicleManager;

namespace LM
{
    class ChangeTracker
    {
    public:
        ChangeTracker(ChangeTracker&) = delete;
        ChangeTracker& operator=(const ChangeTracker&) = delete;
        static ChangeTracker* Instance();
        
        void StartRecordEdit();
        void FinishRecordEdit(bool abort=false);
        bool Undo();
        bool Redo();

        void Clear();
        void Save(std::string path);
        bool Load(std::string path);
        bool LoadStr(std::string path);

        const odr::OpenDriveMap& Map();
    private:
        ChangeTracker() = default;

        void PostLoadActions();

        void PostChangeActions();

        static ChangeTracker* instance;

        odr::OpenDriveMap odrMap;

        struct RoadChange
        {
            boost::optional<odr::Road> before;
            boost::optional<odr::Road> after;
        };

        struct JunctionChange
        {
            boost::optional<odr::Junction> before;
            boost::optional<odr::Junction> after;
        };

        struct MapChange
        {
            std::vector<RoadChange> roadChanges;
            std::vector< JunctionChange> junctionChanges;
        };

        std::stack<MapChange> undoStack;
        std::stack<MapChange> redoStack;

        void RestoreChange(const MapChange& change);
    };
}
