#pragma once
// Pure draft lifecycle decisions, independent of ImGui, persistence and host input.
namespace AlterEgo::EditorLifecycle {
enum class CloseDecision { KeepOpen, Close, ConfirmDiscard };
inline CloseDecision RequestClose(bool requested,bool dirty,bool pending=false){
    return !requested||pending?CloseDecision::KeepOpen:dirty?CloseDecision::ConfirmDiscard:CloseDecision::Close;
}
inline bool CanSave(bool valid,bool dirty,bool creating=false,bool pending=false){
    return valid&&!pending&&(creating||dirty);
}
inline bool CanReplaceEditor(bool open,bool pending=false){return !open&&!pending;}
}
