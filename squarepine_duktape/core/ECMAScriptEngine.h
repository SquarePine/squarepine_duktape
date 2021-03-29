/** The ECMAScriptEngine provides a flexible ECMAScript 5 compliant JavaScript engine
    with an interface implemented by Duktape, but which may be implemented by one of
    many embedded engines in the future.
*/
class ECMAScriptEngine final
{
public:
    //==============================================================================
    /** Constructor. */
    ECMAScriptEngine();

    /** Destructor. */
    ~ECMAScriptEngine();

    //==============================================================================
    /** Evaluates the given code directly in the interpreter.

        @returns the result of the evaluation, or var::undefined() on failure.
    */
    var evaluate (const String& code);

    /** Evaluates an ECMA script compatible code file.

        @param file The file to evaluate.

        @returns the result of the evaluation, or var::undefined() on failure.
    */
    var evaluate (const File& file);

    //==============================================================================
    /** Registers a native method by the given name in the global namespace. */
    void registerNativeMethod (const String&, var::NativeFunction fn);

    /** Registers a native method by the given name on the target object.
    
        The provided target name may be any expression that leaves the target
        object on the top of the stack.

        For example, the following code snippet is equivalent to
        calling the previous `registerNativeMethod` overload
        with just the "hello" and function arguments:

        @code
            registerNativeMethod ("global", "hello",
            [] (const var::NativeFunction&)
            {
                std::cout << "World! " << std::endl;
                return var::undefined();
            });
        @endcode
    */
    void registerNativeMethod (const String&, const String&, var::NativeFunction fn);

    //==============================================================================
    /** Registers a native value by the given name in the global namespace. */
    void registerNativeProperty (const String&, const var&);

    /** Registers a native value by the given name on the target object.
        
        The provided target name may be any expression that leaves the target
        object on the top of the stack

        For example, the following three examples have equivalent behaviour:
        @code
            registerNativeProperty ("global", "hello", "world");
            registerNativeProperty ("hello", "world");
            evaluate ("global.hello = \"world\";");
        @endcode
        
        @throws ECMAScriptError in the event of an evaluation error
    */
    void registerNativeProperty (const String&, const String&, const var&);

    //==============================================================================
    /** Invokes a method, applying the given args, inside the interpreter.
        
        This is similar in function to `Function.prototype.apply()`. The provided
        method name may be any expression that leaves the target function on the
        top of the stack. For example:

        @code
            invoke ("global.dispatchViewEvent", args);
        @endcode

        @returns the result of the invocation, or var::undefined() on failure.
    */
    var invoke (const String& name, const std::vector<var>& vargs);

    /** Invokes a method with the given args inside the interpreter.
        
        This is similar in function to `Function.prototype.call()`. The provided
        method name may be any expression that leaves the target function on the
        top of the stack. For example:

        @code
            invoke ("global.dispatchViewEvent", "click");
        @endcode
        
        @returns the result of the invocation, or var::undefined() on failure.
    */
    template<typename... T>
    var invoke (const String& name, T... args);

    //==============================================================================
    /** Resets the internal context, clearing the value stack and destroying native callbacks. */
    void reset();

    //==============================================================================
    /** Pauses execution and waits for a debug client to attach and begin a debug session. */
    void debuggerAttach();

    /** Detaches the from the current debug session/attachment. */
    void debuggerDetach();

private:
    //==============================================================================
    class Pimpl;
    std::unique_ptr<Pimpl> pimpl;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ECMAScriptEngine)
};

//==============================================================================
template <typename... Types>
var ECMAScriptEngine::invoke (const String& name, Types... args)
{
    // Pack the args and push them to the alternate `invoke` implementation
    std::vector<var> vargs { args... };
    return invoke (name, vargs);
}
