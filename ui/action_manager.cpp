#include "action_manager.h"
#include "road_drawing.h"
#include "main_widget.h"
#include "CreateRoadOptionWidget.h"
#include "change_tracker.h"
#include "util.h"

#include <fstream>
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>
#include <qscrollbar.h>


extern MapView* g_mapView;
extern SectionProfileConfigWidget* g_createRoadOption;

namespace RoadRunner
{
    ActionManager* ActionManager::instance = nullptr;

    ActionManager* ActionManager::Instance()
    {
        if (instance == nullptr)
        {
            instance = new ActionManager;
        }
        return instance;
    }

    ActionManager::ActionManager() : 
        startTime(QTime::currentTime()) {}

    void ActionManager::Record(MapView::EditMode modeChange)
    {
        if (replayMode || !replayable) return;
        ChangeModeAction serialized{ modeChange };
        history.emplace_back(serialized, startTime.msecsTo(QTime::currentTime()));
        Save();
    }

    void ActionManager::Replay(const ChangeModeAction& action)
    {
        g_mapView->parentContainer->SetModeFromReplay(action.mode);
    }

    void ActionManager::Record(double zoomVal, double rotateVal, int hScroll, int vScroll)
    {
        if (replayMode || !replayable) return;
        ChangeViewportAction serialized
        {
            zoomVal, rotateVal,
            hScroll, vScroll
        };
        latestViewportChange.emplace(serialized);
    }

    void ActionManager::Replay(const ChangeViewportAction& action)
    {
        g_mapView->SetViewFromReplay(action.zoom, action.rotate, action.hScroll, action.vScroll);
    }

    void ActionManager::Record(QMouseEvent* evt)
    {
        if (replayMode || !replayable) return;

        FlushBufferedViewportChange();
        MouseAction serialized;
        serialized.x = evt->pos().x();
        serialized.y = evt->pos().y();
        serialized.type = evt->type();
        serialized.button = evt->button();
        if (evt->type() == QEvent::Type::MouseMove)
        {
            latestMouseMove.emplace(serialized);
        }
        else
        {
            FlushBufferedMouseMove();
            history.emplace_back(serialized, startTime.msecsTo(QTime::currentTime()));
            Save();
        }

        if (evt->type() == QEvent::Type::MouseButtonPress)
        {
            auto scenePos = g_mapView->mapToScene(evt->pos());
            spdlog::trace("Record Click: {},{} ( {},{} )-> scene {},{}", 
                evt->pos().x(), evt->pos().y(),
                g_mapView->viewportTransform().dx(), g_mapView->viewportTransform().dy(),
                scenePos.x(), scenePos.y());
        }
    }

    void ActionManager::Replay(const MouseAction& action)
    {
        auto qMouseEvent = std::make_unique<QMouseEvent>(action.type,
            QPointF(action.x, action.y), action.button,
            QFlags<Qt::MouseButton>(), QFlags<Qt::KeyboardModifier>());
        switch (action.type)
        {
        case QEvent::Type::MouseButtonPress:
        {
            auto scenePos = g_mapView->mapToScene(QPoint(action.x, action.y));
            spdlog::trace("Click: {},{} ( {},{} )-> scene {},{}",
                action.x, action.y, 
                g_mapView->viewportTransform().dx(), g_mapView->viewportTransform().dy(),
                scenePos.x(), scenePos.y());
            g_mapView->OnMousePress(qMouseEvent.get());
            break;
        }
        case QEvent::Type::MouseButtonDblClick:
            g_mapView->OnMouseDoubleClick(qMouseEvent.get());
            break;
        case QEvent::Type::MouseMove:
            g_mapView->OnMouseMove(qMouseEvent.get());
            break;
        case QEvent::Type::MouseButtonRelease:
            g_mapView->OnMouseRelease(qMouseEvent.get());
            break;
        default:
            throw;
        }
    }

    void ActionManager::Record(QKeyEvent* evt)
    {
        if (replayMode || !replayable) return;
        FlushBufferedMouseMove();
        KeyPressAction serialized{ evt->key() };
        history.emplace_back(serialized, startTime.msecsTo(QTime::currentTime()));
        Save();
    }

    void ActionManager::Replay(const KeyPressAction& action)
    {
        auto qKeyEvent = std::make_unique<QKeyEvent>(QEvent::Type::KeyPress,
            action.key, QFlags<Qt::KeyboardModifier>());
        g_mapView->OnKeyPress(qKeyEvent.get());
    }

    void ActionManager::Record(const SectionProfile& left, const SectionProfile& right)
    {
        if (replayMode || !replayable) return;
        ChangeProfileAction serialized{ left, right };
        history.emplace_back(serialized, startTime.msecsTo(QTime::currentTime()));
        Save();
    }

    void ActionManager::Replay(const ChangeProfileAction& action)
    {
        g_createRoadOption->SetOption(action.leftProfile, action.rightProfile);
    }

    void ActionManager::Record(ActionType actionNoParm)
    {
        switch (actionNoParm)
        {
        case RoadRunner::Action_Undo: case RoadRunner::Action_Redo:
            history.emplace_back(actionNoParm, startTime.msecsTo(QTime::currentTime()));
            Save();
            break;
        case RoadRunner::Action_LoadMap:
            // To be supported
            replayable = false;
            break;
        default:
            throw;
        }
    }

    void ActionManager::Replay(const UserAction& action)
    {
        history.emplace_back(action);
        switch (action.type)
        {
        case Action_Mouse:
            Replay(lastViewportReplay); // Weird: mapView->transform() changes silently
            Replay(action.detail.mouse);
            break;
        case Action_KeyPress:
            Replay(action.detail.keyPress);
            break;
        case Action_ChangeMode:
            Replay(action.detail.changeMode);
            break;
        case Action_Viewport:
            lastViewportReplay = action.detail.viewport;
            break;
        case Action_ChangeProfile:
            Replay(action.detail.changeProfile);
            break;
        case Action_Undo:
            if (!ChangeTracker::Instance()->Undo())
            {
                spdlog::error("Error replaying undo action");
            }
            break;
        case Action_Redo:
            if (!ChangeTracker::Instance()->Redo())
            {
                spdlog::error("Error replaying redo action");
            }
            break;
        default:
            spdlog::error("Action type {} replay is not supported", static_cast<int>(action.type));
            break;
        }
    }

    void ActionManager::Save() const
    {
        Save(AutosavePath());
    }

    void ActionManager::Save(std::string fpath) const
    {
        std::ofstream outFile(fpath, std::ios::binary);
        cereal::BinaryOutputArchive oarchive(outFile);
        oarchive(history);
    }

    std::string ActionManager::AutosavePath() const
    {
        return RoadRunner::DefaultSaveFolder() + "\\action_rec__" + RoadRunner::RunTimestamp() + ".dat";
    }

    void ActionManager::ReplayImmediate()
    {
        ReplayImmediate(AutosavePath());
    }

    void ActionManager::ReplayImmediate(std::string fpath)
    {
        history.clear();

        std::ifstream inFile(fpath, std::ios::binary);
        cereal::BinaryInputArchive iarchive(inFile);
        std::vector<UserAction> historyCopy;
        iarchive(historyCopy);
        replayMode = true;
        for (const auto& action : historyCopy)
        {
            Replay(action);
        }
        replayMode = false;
    }

    void ActionManager::Reset()
    {
        history.clear();
    }

    void ActionManager::FlushBufferedViewportChange()
    {
        if (!latestViewportChange.has_value()) return;

        history.emplace_back(latestViewportChange.value(), startTime.msecsTo(QTime::currentTime()));
        latestViewportChange.reset();
    }

    void ActionManager::FlushBufferedMouseMove()
    {
        if (!latestMouseMove.has_value()) return;

        history.emplace_back(latestMouseMove.value(), startTime.msecsTo(QTime::currentTime()));
        latestMouseMove.reset();
    }

    std::vector<UserAction> ActionManager::Load(std::string fpath)
    {
        std::ifstream inFile(fpath, std::ios::binary);
        cereal::BinaryInputArchive iarchive(inFile);
        std::vector<UserAction> historyTemp;
        iarchive(historyTemp);
        return historyTemp;
    }
}