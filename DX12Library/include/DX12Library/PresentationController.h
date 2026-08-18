#pragma once

#include <functional>

//Modify Begin:2026-08-18 by Hui
class PresentationController
{
public:
    virtual ~PresentationController() = default;

    virtual bool ReconfigurePresentation(const std::function<bool()>& configureRuntime) = 0;
};
//Modify End
