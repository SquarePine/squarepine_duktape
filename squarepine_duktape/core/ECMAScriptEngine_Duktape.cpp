JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4005 4100 4127 4505 4611 4702)

extern "C"
{
   #if JUCE_WINDOWS
    #undef DUK_USE_DATE_NOW_WINDOWS
    #define DUK_USE_DATE_NOW_WINDOWS 1

    #undef _WINSOCKAPI_
    #define _WINSOCKAPI_ 1
   #endif

    #include "duktape/duktape.c"
    #include "duktape/duk_console.c"

   #if JUCE_WINDOWS
    #include "duktape/duk_trans_socket_windows.c"
   #else
    #include "duktape/duk_trans_socket_unix.c"
   #endif

    #undef inline
}

JUCE_END_IGNORE_WARNINGS_MSVC

//==============================================================================
static void fatalErrorHandler (void*, const char* message)
{
    throw ECMAScriptFatalError (message);
}

static String getContextDump (duk_context* context)
{
    duk_push_context_dump (context);
    auto ret = String (duk_to_string (context, -1));
    duk_pop (context);
    return ret;
}

static void safeCall (duk_context* context, const int numArgs)
{
    if (duk_pcall (context, numArgs) != DUK_EXEC_SUCCESS)
    {
        const String stack = duk_safe_to_stacktrace (context, -1);
        const String msg = duk_safe_to_string (context, -1);

        throw ECMAScriptError (msg, stack, getContextDump (context));
    }
}

static void safeEvalString (duk_context* context, const String& s)
{
    if (duk_peval_string (context, s.toRawUTF8()) != DUK_EXEC_SUCCESS)
    {
        const String stack = duk_safe_to_stacktrace (context, -1);
        const String msg = duk_safe_to_string (context, -1);

        throw ECMAScriptError (msg, stack, getContextDump (context));
    }
}

static void safeCompileFile (duk_context* context, const File& file)
{
    const auto body = file.loadFileAsString();

    duk_push_string (context, file.getFileName().toRawUTF8());

    if (duk_pcompile_string_filename (context, DUK_COMPILE_EVAL, body.toRawUTF8()) != DUK_EXEC_SUCCESS)
    {
        const String stack = duk_safe_to_stacktrace (context, -1);
        const String msg = duk_safe_to_string (context, -1);

        throw ECMAScriptError (msg, stack, getContextDump (context));
    }
}

//==============================================================================
static var javascriptLog (const var::NativeFunctionArgs& args)
{
    for (int i = 0; i < args.numArguments; ++i)
        Logger::writeToLog (args.arguments[i].toString());

    return var::undefined();
}

static var javascriptPlaceholderFunction (const var::NativeFunctionArgs&)
{
    jassertfalse; // Careful - you're calling a function that is low priority to implement.
    return var::undefined();
}

struct ConsoleObject final : DynamicObject
{
    ConsoleObject()
    {
        setMethod ("log", javascriptLog);

        setMethod ("clear", javascriptPlaceholderFunction);
        setMethod ("assert", javascriptPlaceholderFunction);
        setMethod ("count", javascriptPlaceholderFunction);
        setMethod ("countReset", javascriptPlaceholderFunction);
        setMethod ("debug", javascriptPlaceholderFunction);
        setMethod ("dir", javascriptPlaceholderFunction);
        setMethod ("dirxml", javascriptPlaceholderFunction);
        setMethod ("error", javascriptPlaceholderFunction);
        setMethod ("exception", javascriptPlaceholderFunction);
        setMethod ("group", javascriptPlaceholderFunction);
        setMethod ("groupCollapsed", javascriptPlaceholderFunction);
        setMethod ("groupEnd", javascriptPlaceholderFunction);
        setMethod ("info", javascriptPlaceholderFunction);
        setMethod ("profile", javascriptPlaceholderFunction);
        setMethod ("profileEnd", javascriptPlaceholderFunction);
        setMethod ("table", javascriptPlaceholderFunction);
        setMethod ("time", javascriptPlaceholderFunction);
        setMethod ("timeLog", javascriptPlaceholderFunction);
        setMethod ("timeStamp", javascriptPlaceholderFunction);
        setMethod ("trace", javascriptPlaceholderFunction);
        setMethod ("warn", javascriptPlaceholderFunction);
    }
};

//==============================================================================
class ECMAScriptEngine::Pimpl final : private Timer
{
public:
    Pimpl()
    {
        reset();
    }

    ~Pimpl() override
    {
        // NB: Explicitly stopping the timer so as to avoid any late calls to dereferencing a (cleaned up) context.
        stopTimer();
    }

    //==============================================================================
    var evaluate (const String& code)
    {
        jassert (code.isNotEmpty());
        auto* rawContext = dukContext.get();

        try
        {
            safeEvalString (rawContext, code);
        }
        catch (const ECMAScriptError& error)
        {
            reset();
            throw error;
        }

        auto result = readVarFromDukStack (dukContext, -1);
        duk_pop (rawContext);

        return result;
    }

    var evaluate (const File& code)
    {
        jassert (code.existsAsFile());
        jassert (code.loadFileAsString().isNotEmpty());
        auto* rawContext = dukContext.get();

        try
        {
            safeCompileFile (rawContext, code);
            safeCall (rawContext, 0);
        }
        catch (const ECMAScriptError& error)
        {
            reset();
            throw error;
        }

        auto result = readVarFromDukStack (dukContext, -1);
        duk_pop (rawContext);
        return result;
    }
    //==============================================================================
    void registerNativeProperty (const String& name, var value)
    {
        auto* rawContext = dukContext.get();

        duk_push_global_object (rawContext);
        pushVarToDukStack (dukContext, value, true);
        duk_put_prop_string (rawContext, -2, name.toRawUTF8());
        duk_pop (rawContext);
    }

    void registerNativeFunction (const String& name, var::NativeFunction value)
    {
        registerNativeProperty (name, value);
    }

    void registerNativeProperty (const String& target, const String& name, const var& value)
    {
        auto* rawContext = dukContext.get();

        try
        {
            safeEvalString (rawContext, target);
        }
        catch (const ECMAScriptError& error)
        {
            reset();
            throw error;
        }

        pushVarToDukStack (dukContext, value, true);
        duk_put_prop_string (rawContext, -2, name.toRawUTF8());
        duk_pop (rawContext);
    }

    //==============================================================================
    var invoke (const String& name, const std::vector<var>& vargs)
    {
        auto* rawContext = dukContext.get();

        try
        {
            safeEvalString (rawContext, name);

            if (! duk_is_function (rawContext, -1))
                throw ECMAScriptError ("Invocation failed, target is not a function.");

            const auto nargs = static_cast<duk_idx_t> (vargs.size());
            duk_require_stack_top (rawContext, nargs);

            for (auto& p : vargs)
                pushVarToDukStack (dukContext, p);

            safeCall (rawContext, nargs);
        }
        catch (const ECMAScriptError& error)
        {
            reset();
            throw error;
        }

        auto result = readVarFromDukStack (dukContext, -1);
        duk_pop (rawContext);

        return result;
    }

    struct TimeoutFunctionManager final : public MultiTimer
    {
        TimeoutFunctionManager() = default;

        ~TimeoutFunctionManager() override
        {
            for (const auto& v : timeoutFunctions)
                stopTimer (v.first);
        }

        var clearTimeout (int id)
        {
            stopTimer (id);

            const auto f = timeoutFunctions.find (id);
            if (f != timeoutFunctions.cend())
                timeoutFunctions.erase (f);

            return {};
        }

        int newTimeout (var::NativeFunction f, int timeoutMillis, const std::vector<var>&& args, bool repeats = false)
        {
            static int nextId = 0;
            timeoutFunctions.emplace (nextId, TimeoutFunction (f, std::move (args), repeats));
            startTimer (nextId, timeoutMillis);
            return nextId++;
        }

        void timerCallback (int timerID) override
        {
            const auto f = timeoutFunctions.find (timerID);

            if (f != timeoutFunctions.cend())
            {
                const auto cb = f->second;
                std::invoke (cb.f, var::NativeFunctionArgs (var(), cb.args.data(), static_cast<int> (cb.args.size())));

                if (! cb.repeats)
                {
                    stopTimer (timerID);
                    timeoutFunctions.erase (f);
                }
            }
        }

    private:
        struct TimeoutFunction final
        {
            TimeoutFunction (var::NativeFunction _f, const std::vector<var>&& _args, bool _repeats = false)
                : f (_f), args (std::move (_args)), repeats (_repeats) {}

            const var::NativeFunction f;
            std::vector<var> args;
            const bool repeats;
        };

        std::map<int, TimeoutFunction> timeoutFunctions;
    };

    // IsSetter is true for setTimeout / setInterval
    // and false for clearTimeout / clearInterval
    template <bool IsSetter = false, bool Repeats = false, typename MethodType>
    void registerNativeTimerFunction (const char* name, MethodType method)
    {
        registerNativeProperty (name,
            var::NativeFunction ([this, name, method] (const var::NativeFunctionArgs& _args) -> var
            {
                if constexpr (IsSetter)
                {
                    if (_args.numArguments < 2 || ! _args.arguments[0].isMethod() || ! _args.arguments[1].isDouble())
                        throw ECMAScriptError (String (name) + " requires a callback and time in milliseconds");

                    std::vector<var> args (_args.arguments + 2, _args.arguments + _args.numArguments);
                    return (timeoutsManager.get()->*method) (_args.arguments[0].getNativeFunction(), _args.arguments[1], std::move (args), Repeats);
                }
                else
                {
                    if (_args.numArguments < 1 || ! _args.arguments[0].isDouble())
                        throw ECMAScriptError (String (name) + " requires an integer ID of the timer to clear");

                    return (timeoutsManager.get()->*method) (_args.arguments[0]);
                }
            }));
    }

    void registerTimerGlobals()
    {
        registerNativeTimerFunction<true> ("setTimeout", &TimeoutFunctionManager::newTimeout);
        registerNativeTimerFunction<true, true> ("setInterval", &TimeoutFunctionManager::newTimeout);
        registerNativeTimerFunction ("clearTimeout", &TimeoutFunctionManager::clearTimeout);
        registerNativeTimerFunction ("clearInterval", &TimeoutFunctionManager::clearTimeout);
    }

    void reset()
    {
        // Clear out any timer callbacks
        timeoutsManager = std::make_unique<TimeoutFunctionManager>();

        // Allocate a new js heap
        dukContext = std::shared_ptr<duk_context> (
            duk_create_heap (nullptr, nullptr, nullptr, nullptr, fatalErrorHandler),
            duk_destroy_heap
        );

        // Add console.log support
        auto* rawContext = dukContext.get();
        duk_console_init (rawContext, DUK_CONSOLE_FLUSH);

        // Install a pointer back to this ECMAScriptEngine instance
        duk_push_global_stash (rawContext);
        duk_push_pointer (rawContext, (void*) this);
        duk_put_prop_string (rawContext, -2, DUK_HIDDEN_SYMBOL ("__EcmascriptEngineInstance__"));
        duk_pop (rawContext);

        persistentReleasePool.clear();

        registerTimerGlobals();

        registerNativeProperty ("console", new ConsoleObject());
        registerNativeFunction ("print", javascriptLog);
        registerNativeFunction ("log", javascriptLog);
    }

    //==============================================================================
    void debuggerAttach()
    {
        auto* rawContext = dukContext.get();

        duk_trans_socket_init();
        duk_trans_socket_waitconn();

        duk_debugger_attach (rawContext,
                             duk_trans_socket_read_cb,
                             duk_trans_socket_write_cb,
                             duk_trans_socket_peek_cb,
                             duk_trans_socket_read_flush_cb,
                             duk_trans_socket_write_flush_cb,
                             nullptr,
                             [] (duk_context*, void* data)
                             {
                                 duk_trans_socket_finish();
                            
                                 static_cast<ECMAScriptEngine::Pimpl*> (data)->stopTimer();
                             },
                             this);

        // Start timer for duk_debugger_cooperate calls
        startTimer (200);
    }

    void debuggerDetach()
    {
        if (auto* dc = dukContext.get())
            duk_debugger_detach (dc);
    }

    //==============================================================================
    void timerCallback() override
    {
        if (auto* dc = dukContext.get())
            duk_debugger_cooperate (dc);
    }

    //==============================================================================
    struct LambdaHelper
    {
        LambdaHelper (var::NativeFunction fn, uint32_t _id)
            : callback (std::move (fn)), id (_id) {}

        static duk_ret_t invokeFromDukContext (duk_context* context)
        {
            // First we have to retrieve the actual function pointer and our engine pointer
            // See: https://duktape.org/guide.html#hidden-symbol-properties
            duk_push_current_function (context);
            duk_get_prop_string (context, -1, DUK_HIDDEN_SYMBOL ("LambdaHelperPtr"));

            auto* helper = static_cast<LambdaHelper*> (duk_get_pointer (context, -1));
            duk_pop (context);

            // Then the engine...
            duk_get_prop_string (context, -1, DUK_HIDDEN_SYMBOL ("EnginePtr"));
            auto* engine = static_cast<ECMAScriptEngine::Pimpl*> (duk_get_pointer (context, -1));

            // Pop back both the pointer and the "current function"
            duk_pop_2 (context);

            // Now we can collect our args
            std::vector<var> args;
            const int nargs = duk_get_top (context);

            for (int i = 0; i < nargs; ++i)
                args.push_back (engine->readVarFromDukStack (engine->dukContext, i));

            var result;

            // Now we can invoke the user method with its arguments
            try
            {
                result = std::invoke (helper->callback, var::NativeFunctionArgs (
                    var(),
                    args.data(),
                    static_cast<int> (args.size())
                ));
            }
            catch (const ECMAScriptError& error)
            {
                duk_push_error_object (context, DUK_ERR_TYPE_ERROR, error.what());
                return duk_throw (context);
            }

            // For an undefined result, return 0 to notify the duktape interpreter
            if (result.isUndefined())
                return 0;

            // Otherwise, push the result to the stack and tell duktape
            engine->pushVarToDukStack (engine->dukContext, result);
            return 1;
        }

        static duk_ret_t invokeFromDukContextLightFunc (duk_context* context)
        {
            // Retrieve the engine pointer
            duk_push_global_stash (context);
            duk_get_prop_string (context, -1, DUK_HIDDEN_SYMBOL ("__EcmascriptEngineInstance__"));

            auto* engine = static_cast<ECMAScriptEngine::Pimpl*> (duk_get_pointer (context, -1));
            duk_pop_2 (context);

            // Retrieve the lambda helper
            duk_push_current_function (context);
            const auto magic = duk_get_magic (context, -1);
            auto& helper = engine->temporaryReleasePool[static_cast<size_t> (magic + 128)];
            duk_pop (context);

            // Now we can collect our args
            const int nargs = duk_get_top (context);
            std::vector<var> args;
            args.reserve (static_cast<size_t> (nargs));

            for (int i = 0; i < nargs; ++i)
                args.push_back (engine->readVarFromDukStack (engine->dukContext, i));

            // Now we can invoke the user method with its arguments
            const auto result = std::invoke (helper->callback, var::NativeFunctionArgs (var(), args.data(), static_cast<int> (args.size())));

            // For an undefined result, return 0 to notify the duktape interpreter
            if (result.isUndefined())
                return 0;

            // Otherwise, push the result to the stack and tell duktape
            engine->pushVarToDukStack (engine->dukContext, result);
            return 1;
        }

        static duk_ret_t callbackFinalizer (duk_context* context)
        {
            // First we have to retrieve the actual function pointer and our engine pointer
            // See: https://duktape.org/guide.html#hidden-symbol-properties
            // And: https://duktape.org/api.html#duk_set_finalizer
            // In this case our function is at index 0.
            duk_require_function (context, 0);
            duk_get_prop_string (context, 0, DUK_HIDDEN_SYMBOL ("LambdaHelperPtr"));

            auto* helper = static_cast<LambdaHelper*> (duk_get_pointer (context, -1));
            duk_pop (context);

            // Then the engine...
            duk_get_prop_string (context, 0, DUK_HIDDEN_SYMBOL ("EnginePtr"));
            auto* engine = static_cast<ECMAScriptEngine::Pimpl*> (duk_get_pointer (context, -1));

            // Pop back both the pointer and the "current function"
            duk_pop_2 (context);

            // Clean up our lambda helper
            engine->removeLambdaHelper (helper);
            return 0;
        }

        var::NativeFunction callback;
        uint32_t id;
    };

    //==============================================================================
    /** Helper for cleaning up native function temporaries. */
    void removeLambdaHelper (LambdaHelper* helper)
    {
        persistentReleasePool.erase (helper->id);
    }

    /** Helper for pushing a var to the duktape stack. */
    void pushVarToDukStack (std::shared_ptr<duk_context> context, var v, bool persistNativeFunctions = false)
    {
        auto* rawContext = dukContext.get();

        if (v.isVoid() || v.isUndefined())      { duk_push_undefined (rawContext); return; }
        else if (v.isBool())                    { duk_push_boolean (rawContext, (bool) v); return; }
        else if (v.isInt())                     { duk_push_int (rawContext, (int) v); return; }
        else if (v.isInt64())                   { duk_push_number (rawContext, (double) v); return; } // Because duktape sucks...
        else if (v.isDouble())                  { duk_push_number (rawContext, (double) v); return; }
        else if (v.isString())                  { duk_push_string (rawContext, v.toString().toRawUTF8()); return; }
        else if (v.isArray())
        {
            auto arr_idx = duk_push_array (rawContext);
            duk_uarridx_t i = 0;

            for (auto& e : *v.getArray())
            {
                pushVarToDukStack (context, e, persistNativeFunctions);
                duk_put_prop_index (rawContext, arr_idx, i++);
            }

            return;
        }
        else if (v.isObject())
        {
            if (auto* o = v.getDynamicObject())
            {
                auto obj_idx = duk_push_object (rawContext);

                for (auto& e : o->getProperties())
                {
                    pushVarToDukStack (context, e.value, persistNativeFunctions);
                    duk_put_prop_string (rawContext, obj_idx, e.name.toString().toRawUTF8());
                }
            }

            return;
        }
        else if (v.isMethod())
        {
            if (persistNativeFunctions)
            {
                // For persisted native functions, we provide a helper layer storing and retrieving the
                // stash, and marshalling between the Duktape C interface and the NativeFunction interface
                duk_push_c_function (rawContext, LambdaHelper::invokeFromDukContext, DUK_VARARGS);

                // Now we assign the pointers as properties of the wrapper function
                auto helper = std::make_unique<LambdaHelper> (v.getNativeFunction(), nextHelperId++);
                duk_push_pointer (rawContext, (void *) helper.get());
                duk_put_prop_string (rawContext, -2, DUK_HIDDEN_SYMBOL ("LambdaHelperPtr"));
                duk_push_pointer (rawContext, (void *) this);
                duk_put_prop_string (rawContext, -2, DUK_HIDDEN_SYMBOL ("EnginePtr"));

                // Now we prepare the finalizer
                duk_push_c_function (rawContext, LambdaHelper::callbackFinalizer, 1);
                duk_push_pointer (rawContext, (void *) helper.get());
                duk_put_prop_string (rawContext, -2, DUK_HIDDEN_SYMBOL ("LambdaHelperPtr"));
                duk_push_pointer (rawContext, (void *) this);
                duk_put_prop_string (rawContext, -2, DUK_HIDDEN_SYMBOL ("EnginePtr"));
                duk_set_finalizer (rawContext, -2);

                // And hang on to it! 
                persistentReleasePool[helper->id] = std::move (helper);
            }
            else
            {
                // For temporary native functions, we use the stack-allocated lightfunc. In
                // this case we can't attach properties, so we can't rely on raw pointers to
                // the LambdaHelper and we can't rely on finalizers. So, all we do here is use
                // a small pool for temporary LambdaHelpers. Within this pool, we just allow insertions
                // to wrap around and clobber previous temporaries, effectively garbage collecting on
                // demand. The maximum number of temporary values before wrapping is 255, as dictated
                // by that we use the lightfunc's magic number to identify our native callback.
                auto helper = std::make_unique<LambdaHelper> (v.getNativeFunction(), nextHelperId++);
                auto magic = nextMagicInt++;

                duk_push_c_lightfunc (rawContext, LambdaHelper::invokeFromDukContextLightFunc, DUK_VARARGS, 15, magic);
                temporaryReleasePool[static_cast<size_t> (magic + 128)] = std::move (helper);

                if (nextMagicInt >= 127)
                    nextMagicInt = -128;
            }

            return;
        }

        // If you hit this, you tried to push an unsupported var type to the duktape stack.
        jassertfalse;
    }

    /** Helper for reading from the duktape stack to a var instance. */
    var readVarFromDukStack (std::shared_ptr<duk_context> context, duk_idx_t idx)
    {
        auto* rawContext = dukContext.get();
        var value;

        switch (duk_get_type (rawContext, idx))
        {
            case DUK_TYPE_NULL:
                // It looks like var doesn't have an explicit null value,
                // so we're just using the default empty constructor value.
            break;

            case DUK_TYPE_UNDEFINED:    value = var::undefined(); break;
            case DUK_TYPE_BOOLEAN:      value = (bool) duk_get_boolean (rawContext, idx);  break;
            case DUK_TYPE_NUMBER:       value = duk_get_number (rawContext, idx); break;
            case DUK_TYPE_STRING:       value = String (CharPointer_UTF8 (duk_get_string (rawContext, idx))); break;

            case DUK_TYPE_OBJECT:
            case DUK_TYPE_LIGHTFUNC:
            {
                if (duk_is_array (rawContext, idx))
                {
                    duk_size_t len = duk_get_length (rawContext, idx);
                    Array<var> els;

                    for (duk_size_t i = 0; i < len; ++i)
                    {
                        duk_get_prop_index (rawContext, idx, static_cast<duk_uarridx_t> (i));
                        els.add (readVarFromDukStack (context, -1));
                        duk_pop (rawContext);
                    }

                    value = els;
                    break;
                }

                if (duk_is_function (rawContext, idx) || duk_is_lightfunc (rawContext, idx))
                {
                    struct CallbackHelper {
                        CallbackHelper (std::weak_ptr<duk_context> _weakContext)
                            : weakContext (_weakContext)
                            , funcId (String ("__NativeCallback__") + Uuid().toString()) {}

                        ~CallbackHelper() {
                            if (auto spt = weakContext.lock()) {
                                duk_push_global_stash (spt.get());
                                duk_del_prop_string (spt.get(), -1, funcId.toRawUTF8());
                                duk_pop (spt.get());
                            }
                        }

                        std::weak_ptr<duk_context> weakContext;
                        String funcId;
                    };

                    // With a function, we first push the function reference to
                    // the Duktape global stash so we can read it later.
                    auto helper = std::make_shared<CallbackHelper> (context);

                    duk_push_global_stash (rawContext);
                    duk_dup (rawContext, idx);
                    duk_put_prop_string (rawContext, -2, helper->funcId.toRawUTF8());
                    duk_pop (rawContext);

                    // Next we create a var::NativeFunction that captures the function
                    // id and knows how to invoke it
                    value = var::NativeFunction {
                        [this, weakContext = std::weak_ptr<duk_context> (context), helper] (const var::NativeFunctionArgs& args) -> var {
                            auto sharedContext = weakContext.lock();

                            // If our context disappeared, we return early
                            if (! sharedContext)
                                return var();

                            auto* rawPtr = sharedContext.get();

                            // Here when we're being invoked we retrieve the callback function from
                            // the global stash and invoke it with the provided args.
                            duk_push_global_stash (rawPtr);
                            duk_get_prop_string (rawPtr, -1, helper->funcId.toRawUTF8());

                            if (! (duk_is_lightfunc (rawPtr, -1) || duk_is_function (rawPtr, -1)))
                                throw ECMAScriptError ("Global callback not found.", "", getContextDump (rawPtr));

                            // Push the args to the duktape stack
                            duk_require_stack_top (rawPtr, args.numArguments);

                            for (int i = 0; i < args.numArguments; ++i)
                                pushVarToDukStack (sharedContext, args.arguments[i]);

                            // Invocation
                            try
                            {
                                safeCall (rawPtr, args.numArguments);
                            }
                            catch (const ECMAScriptError& error)
                            {
                                reset();
                                throw error;
                            }

                            // Clean the result and the stash off the top of the stack
                            var result = readVarFromDukStack (sharedContext, -1);
                            duk_pop_2 (rawPtr);

                            return result;
                        }
                    };

                    break;
                }

                // If it's not a function or an array, it's a regular object.
                auto* obj = new DynamicObject();

                // Generic object enumeration; `duk_enum` pushes an enumerator
                // object to the top of the stack
                duk_enum (rawContext, idx, DUK_ENUM_OWN_PROPERTIES_ONLY);

                while (duk_next (rawContext, -1, 1))
                {
                    // For each found key/value pair, `duk_enum` pushes the
                    // values to the top of the stack. So here the stack top
                    // is [ ... enum key value]. Enum is at -3, key at -2,
                    // value at -1 from the stack top.
                    // Note here that all keys in an ECMAScript object are of
                    // type string, even arrays, e.g. `myArr[0]` has an implicit
                    // conversion from number to string. Thus here, while constructing
                    // the DynamicObject, we take the `toString()` value for the key
                    // always.
                    obj->setProperty (duk_to_string (rawContext, -2), readVarFromDukStack (context, -1));

                    // Clear the key/value pair from the stack
                    duk_pop_2 (rawContext);
                }

                // Pop the enumerator from the stack
                duk_pop (rawContext);

                value = var (obj);
            }
            break;

            case DUK_TYPE_NONE:
            default:
                jassertfalse;
            break;
        }

        return value;
    }

    //==============================================================================
    uint32_t nextHelperId = 0;
    int32_t nextMagicInt = 0;
    std::unordered_map<uint32_t, std::unique_ptr<LambdaHelper>> persistentReleasePool;
    std::array<std::unique_ptr<LambdaHelper>, 255> temporaryReleasePool;
    std::unique_ptr<TimeoutFunctionManager> timeoutsManager;

    // The duk_context must be listed after the release pools so that it is destructed
    // before the pools. That way, as the duk_context is being freed and finalizing all
    // of our lambda helpers, our pools still exist for those code paths.
    std::shared_ptr<duk_context> dukContext;
};
