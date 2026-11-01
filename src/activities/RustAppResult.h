#pragma once
#include <xp_app.h>

#include "ActivityResult.h"

XpAppResultView rustAppResultView(const ActivityResult& result);
ActivityResult nativeAppResult(const XpAppResultView& result);
