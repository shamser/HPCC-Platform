/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2020 HPCC Systems®.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

#ifndef WUCOSTING_H
#define WUCOSTING_H

#include "jliball.hpp"
#include "eclagent.ipp"

interface ICostCalculator
{
    virtual void updateCosts(IWorkUnit * wu, CostParameters costParameters) = 0;
    virtual int64_t calculateCost(EclSubGraph subgraph, CostParameters costParameter) const = 0;
};

ICostCalculator *createCostCalculator(IWorkUnit * wu, CostParameters costParameters)
#endif
