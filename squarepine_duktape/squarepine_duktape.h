#ifndef SQUAREPINE_MODULE_DUKTAPE_H
#define SQUAREPINE_MODULE_DUKTAPE_H

/** BEGIN_JUCE_MODULE_DECLARATION

    ID:              	squarepine_duktape
    vendor:          	SquarePine
    version:         	1.0.0
    name:            	DAW Duktape
    description:     	An easy to integrate Duktape (Javascript) wrapper in a JUCE module format.
    website:         	http://www.squarepine.io
    license:         	ISC

    dependencies:       juce_events

    END_JUCE_MODULE_DECLARATION
*/
//==============================================================================
#include <juce_events/juce_events.h>

#include <unordered_map>

//==============================================================================
namespace duktape
{
    using namespace juce;

    #include "core/ECMAScriptEngine.h"
}

#endif //SQUAREPINE_MODULE_DUKTAPE_H
