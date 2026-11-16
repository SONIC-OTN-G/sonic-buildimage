#pragma once
#include <atomic>

enum OrchState {
    ORCH_STATE_NOT_READY,
    ORCH_STATE_READY,
    ORCH_STATE_WORK,
    ORCH_STATE_PAUSE,
};

class OrchFSM
{
public:
    static OrchFSM &getInstance();
    static void setState(OrchState state);
    static OrchState getState();

private:
    OrchFSM() = default;
    ~OrchFSM() = default;

    std::atomic<OrchState> m_state = { ORCH_STATE_NOT_READY };
};

