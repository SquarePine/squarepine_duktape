ECMAScriptEngine::ECMAScriptEngine() :
    pimpl (std::make_unique<Pimpl>())
{
    /** If you hit this, you're probably trying to run a console application.

        Please make use of ScopedJuceInitialiser_GUI because this JS engine requires event loops.
        Without the initialiser, the console app would always crash on exit,
        and things will probably not get cleaned up.
    */
    jassert (MessageManager::getInstanceWithoutCreating() != nullptr);
}

ECMAScriptEngine::~ECMAScriptEngine()
{
}

//==============================================================================
var ECMAScriptEngine::evaluate (const String& code)
{
    try
    {
        return pimpl->evaluate (code);
    }
    catch (const ECMAScriptError& err)
    {
        Logger::writeToLog (err.context);
        Logger::writeToLog (err.stack);
        jassertfalse;
    }
    catch (...)
    {
        jassertfalse;
    }

    return {};
}

var ECMAScriptEngine::evaluate (const File& code)
{
    try
    {
        return pimpl->evaluate (code);
    }
    catch (const ECMAScriptError& err)
    {
        Logger::writeToLog (err.context);
        Logger::writeToLog (err.stack);
        jassertfalse;
    }
    catch (...)
    {
        jassertfalse;
    }

    return {};
}

//==============================================================================
void ECMAScriptEngine::registerNativeMethod (const String& name, var::NativeFunction fn)
{
    registerNativeProperty (name, var (fn));
}

void ECMAScriptEngine::registerNativeMethod (const String& target, const String& name, var::NativeFunction fn)
{
    registerNativeProperty (target, name, var (fn));
}

//==============================================================================
void ECMAScriptEngine::registerNativeProperty (const String& name, const var& value)
{
    pimpl->registerNativeProperty (name, value);
}

void ECMAScriptEngine::registerNativeProperty (const String& target, const String& name, const var& value)
{
    pimpl->registerNativeProperty (target, name, value);
}

//==============================================================================
var ECMAScriptEngine::invoke (const String& name, const std::vector<var>& vargs)
{
    return pimpl->invoke (name, vargs);
}

void ECMAScriptEngine::reset()
{
    pimpl->reset();
}

//==============================================================================
void ECMAScriptEngine::debuggerAttach()
{
    pimpl->debuggerAttach();
}

void ECMAScriptEngine::debuggerDetach()
{
    pimpl->debuggerDetach();
}
