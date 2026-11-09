#pragma once
namespace ra2::app {
enum class CameoClick { None, RemoveQueued, Hold, Cancel, Resume, Place, Queue, Begin };
inline CameoClick cameo_click(bool right,bool active,bool complete,bool held,bool pending,bool queueable) {
    if(right) {
        if(queueable && pending)return CameoClick::RemoveQueued;
        if(!active)return CameoClick::None;
        return complete||held?CameoClick::Cancel:CameoClick::Hold;
    }
    if(active && held && !complete)return CameoClick::Resume;
    if(active && complete && !queueable)return CameoClick::Place;
    return queueable?CameoClick::Queue:CameoClick::Begin;
}
}
