#ifndef FLAGS_H
#define FLAGS_H

#include <utility>

template<typename Enum>
class Flags
{
private:
    Enum _value = static_cast<Enum>(0);

public:
    Flags() = default;
    // cppcheck-suppress noExplicitConstructor
    Flags(Enum value) : // NOLINT
        _value(value)
    {}

    template<typename... Tail>
    Flags(Enum value, Tail... values) // NOLINT
    {
        set(value);
        set(values...);
    }

    void set(Enum value)
    {
        _value = static_cast<Enum>(static_cast<int>(_value) | static_cast<int>(value));
    }

    template<typename... Tail>
    void set(Enum value, Tail... values)
    {
        set(value);
        set(values...);
    }

    void reset(Enum value)
    {
        _value = static_cast<Enum>(static_cast<int>(_value) & ~static_cast<int>(value));
    }

    template<typename... Tail>
    void reset(Enum value, Tail... values)
    {
        reset(value);
        reset(values...);
    }

    void setState(Enum value, bool state)
    {
        if(state)
            set(value);
        else
            reset(value);
    }

    bool test(Enum value) const
    {
        return (static_cast<int>(_value) & static_cast<int>(value)) != 0;
    }

    bool anyOf(Enum value) const { return test(value); }
    template<typename... Tail>
    bool anyOf(Enum value, Tail... values) const
    {
        return test(value) || anyOf(values...);
    }

    bool allOf(Enum value) const { return test(value); }
    template<typename... Tail>
    bool allOf(Enum value, Tail... values) const
    {
        return allOf(value) && allOf(values...);
    }

    Enum operator*() const { return _value; }

    //FIXME: Should be able to replace this with a C++17 template deduction constructor
    template<typename... Args>
    static Enum combine(Args... values)
    {
        Flags<Enum> flags;
        flags.set(std::forward<Args>(values)...);
        return *flags;
    }
};

#endif // FLAGS_H
