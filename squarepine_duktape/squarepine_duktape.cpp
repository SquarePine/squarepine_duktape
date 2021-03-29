#ifdef JUCE_AUDIO_DEVICES_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "daw_duktape.h"

namespace duktape
{
	using namespace juce;

    //==============================================================================
    /** A helper for representing an error that occured within the engine. */
    struct ECMAScriptError final : public std::runtime_error
    {
        ECMAScriptError (const String& msg = {},
                         const String& _stack = {},
                         const String& _context = {}) :
            std::runtime_error (msg.toStdString()),
            stack (_stack),
            context (_context)
        {
        }

        const String stack, context;
    };

    /** A helper for representing an error that occured within the engine. */
    struct ECMAScriptFatalError final : public std::runtime_error
    {
        ECMAScriptFatalError (const String& msg) :
            std::runtime_error (msg.toStdString())
        {
        }
    };

	#include "core/ECMAScriptEngine_Duktape.cpp"
	#include "core/ECMAScriptEngine.cpp"
}
