#include "app_state.h"

AppState &AppState::instance()
{
    static AppState state;
    return state;
}
