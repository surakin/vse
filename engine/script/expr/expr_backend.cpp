#include "script/expr/expr_backend.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "cstr/number.h"
#include "os/os.h"
#include "script/backend_registry.h"

namespace engine_placeholder::script::expr
{

namespace
{

using Text = String<HeapAllocator>;

constexpr usize kMaxStringBytes = usize{1} << 20;
constexpr usize kMaxFrames = 64;
constexpr usize kMaxStack = usize{1} << 16;
constexpr u64 kLoadInstructionBudget = 10'000'000;

Text MakeText(StringView sv)
{
    return Text(HeapAllocator::Default(), sv);
}

// ---------------------------------------------------------------- lexer

enum class Tok : u8
{
    End,
    Newline,
    Number,
    String,
    Ident,
    KwDef,
    KwEnd,
    KwIf,
    KwElse,
    KwWhile,
    KwReturn,
    KwAnd,
    KwOr,
    KwNot,
    KwTrue,
    KwFalse,
    KwNil,
    KwGlobal,
    LParen,
    RParen,
    Comma,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Assign,
    EqEq,
    NotEq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
};

struct Token
{
    Tok kind = Tok::End;
    StringView text;  // identifiers and keywords: into the source
    u32 string_index = 0;
    bool is_float = false;
    i64 int_value = 0;
    f64 float_value = 0.0;
    u32 line = 1;
};

bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool IsIdentStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

Tok KeywordKind(StringView word)
{
    struct Entry
    {
        const char* text;
        Tok kind;
    };
    static const Entry entries[] = {{"def", Tok::KwDef},      {"end", Tok::KwEnd},     {"if", Tok::KwIf},
                                    {"else", Tok::KwElse},    {"while", Tok::KwWhile}, {"return", Tok::KwReturn},
                                    {"and", Tok::KwAnd},      {"or", Tok::KwOr},       {"not", Tok::KwNot},
                                    {"true", Tok::KwTrue},    {"false", Tok::KwFalse}, {"nil", Tok::KwNil},
                                    {"global", Tok::KwGlobal}};
    for (const Entry& e : entries)
    {
        if (word == StringView(e.text, CStrLength(e.text)))
        {
            return e.kind;
        }
    }
    return Tok::Ident;
}

// ---------------------------------------------------------------- code

enum class Op : u8
{
    Const,
    Nil,
    True,
    False,
    LoadLocal,
    StoreLocal,
    LoadGlobal,
    StoreGlobal,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Not,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    Jump,
    JumpIfFalse,
    JumpIfFalseKeep,
    JumpIfTrueKeep,
    Call,
    Return,
    Pop,
};

struct Instr
{
    Op op = Op::Nil;
    i32 a = 0;
    i32 b = 0;
    u32 line = 0;
};

struct Function
{
    Function() : name(HeapAllocator::Default()), chunk(HeapAllocator::Default()) {}
    Text name;
    Text chunk;
    u32 params = 0;
    u32 locals = 0;
    Vector<Instr> code;
    Vector<Value> consts;
    Vector<Text> names;
};

struct Frame
{
    const Function* fn = nullptr;
    usize pc = 0;
    usize base = 0;
};

struct Native
{
    Native() : name(HeapAllocator::Default()) {}
    Text name;
    NativeFunction function = nullptr;
    void* user = nullptr;
};

struct Global
{
    Global() : name(HeapAllocator::Default()) {}
    Text name;
    Value value;
};

struct Coroutine
{
    u64 id = 0;
    Vector<Value> stack;
    Vector<Frame> frames;
};

const char* KindName(const Value& v)
{
    switch (v.Kind())
    {
    case ValueKind::Nil: return "nil";
    case ValueKind::Bool: return "boolean";
    case ValueKind::Int:
    case ValueKind::Float: return "number";
    case ValueKind::Text: return "string";
    default: return "userdata";
    }
}

bool Truthy(const Value& v)
{
    return !(v.IsNil() || (v.Kind() == ValueKind::Bool && !v.AsBool()));
}

class ExprState;

// ---------------------------------------------------------------- compiler

constexpr u32 kMaxNesting = 100;

class Compiler
{
public:
    Compiler(ExprState& state, StringView chunk, StringView source);

    // Compiles the chunk into `main`; returns false and sets Error()/ErrorLine() on a syntax error.
    bool Compile(Function* main);
    const Text& Error() const { return error_; }
    u32 ErrorLine() const { return error_line_; }

private:
    const Token& Peek() const { return tokens_[pos_]; }
    bool At(Tok kind) const { return tokens_[pos_].kind == kind; }
    Token Take() { return tokens_[pos_ < tokens_.Size() - 1 ? pos_++ : pos_]; }
    bool Accept(Tok kind)
    {
        if (At(kind))
        {
            Take();
            return true;
        }
        return false;
    }
    void Fail(const char* message);
    bool Failed() const { return failed_; }

    void Lex(StringView source);
    void SkipNewlines()
    {
        while (At(Tok::Newline))
        {
            Take();
        }
    }

    usize Emit(Op op, i32 a = 0, i32 b = 0);
    void Patch(usize at) { fn_->code[at].a = static_cast<i32>(fn_->code.Size()); }
    i32 AddConst(Value v);
    i32 AddName(StringView name);
    i32 FindLocal(StringView name) const;

    void Block();
    void Statement();
    void DefStatement();
    void IfStatement();
    void WhileStatement();
    void ReturnStatement();
    void Assign(StringView name, bool force_global);
    void Expression() { OrExpr(); }
    void OrExpr();
    void AndExpr();
    void Comparison();
    void Additive();
    void Multiplicative();
    void Unary();
    void Primary();

    // Counts the nesting of blocks and expressions so a hostile source cannot overflow the compiler's stack.
    class DepthGuard
    {
    public:
        explicit DepthGuard(Compiler* c) : c_(c) { ++c_->depth_; }
        ~DepthGuard() { --c_->depth_; }
        bool TooDeep()
        {
            if (c_->depth_ > kMaxNesting)
            {
                c_->Fail("source nested too deeply");
                return true;
            }
            return false;
        }

    private:
        Compiler* c_;
    };

    ExprState& state_;
    Text chunk_;
    Vector<Token> tokens_;
    Vector<Text> strings_;
    usize pos_ = 0;
    Function* fn_ = nullptr;
    bool in_function_ = false;
    Vector<Text> local_names_;
    bool failed_ = false;
    u32 depth_ = 0;
    Text error_;
    u32 error_line_ = 0;
};

// ---------------------------------------------------------------- state

class ExprState : public State
{
public:
    ExprState() : last_error_(HeapAllocator::Default()) {}

    // Defines (or replaces) a script function; the old one stays alive for coroutines still inside it.
    void DefineFunction(UniquePtr<Function> fn)
    {
        for (usize i = 0; i < names_.Size(); ++i)
        {
            if (names_[i]->name.View() == fn->name.View())
            {
                names_[i] = fn.Get();
                functions_.Push(Move(fn));
                return;
            }
        }
        names_.Push(fn.Get());
        functions_.Push(Move(fn));
    }

protected:
    Outcome DoLoad(StringView chunk_name, StringView source) override;
    Outcome DoCall(StringView path, Span<const Value> args, const Budget& budget, CallOutput* out) override;
    Outcome DoResume(u64 coroutine, Span<const Value> values, const Budget& budget, CallOutput* out) override;
    void DoAbandon(u64 coroutine) override;
    Outcome DoRegisterFunction(StringView name, NativeFunction function, void* user) override;
    void DoCollect() override {}
    usize DoMemoryUsed() override;
    void DoClose() override;
    Capabilities DoCapabilities() const override;

private:
    enum class RunResult : u8
    {
        Done,
        Yielded,
        Failed,
    };

    const Function* FindFunction(StringView name) const;
    const Native* FindNative(StringView name) const;
    Global* FindGlobal(StringView name);
    Global& GlobalFor(StringView name);
    Outcome Fail(Status status, const Coroutine& co, StringView message);
    Outcome Finish(RunResult result, Coroutine* co, const Budget& budget, CallOutput* out, Outcome* failure);
    // Runs `co` until it returns, yields or fails. On Done the results are on `results`; on Yielded `wait` says why; on
    // Failed `failure` says what.
    RunResult Run(Coroutine* co, const Budget& budget, Vector<Value>* results, WaitRequest* wait, Outcome* failure);
    Outcome StartAndRun(const Function* fn, Span<const Value> args, const Budget& budget, CallOutput* out);
    Outcome FinishRun(RunResult result, UniquePtr<Coroutine> co, CallOutput* out, Outcome failure);

    friend class Compiler;

    Vector<UniquePtr<Function>> functions_;  // every definition ever made
    Vector<const Function*> names_;          // the latest definition of each name
    Vector<Native> natives_;
    Vector<Global> globals_;
    Vector<UniquePtr<Coroutine>> coroutines_;
    u64 next_coroutine_ = 1;
    Text last_error_;
    bool closed_ = false;
};

// ---- compiler implementation

Compiler::Compiler(ExprState& state, StringView chunk, StringView source)
    : state_(state), chunk_(MakeText(chunk)), error_(HeapAllocator::Default())
{
    Lex(source);
}

void Compiler::Fail(const char* message)
{
    if (!failed_)
    {
        failed_ = true;
        error_ = MakeText(message);
        error_line_ = Peek().line;
    }
}

void Compiler::Lex(StringView s)
{
    u32 line = 1;
    usize i = 0;
    const usize n = s.Length();
    auto push = [&](Tok kind)
    {
        Token t;
        t.kind = kind;
        t.line = line;
        tokens_.Push(t);
    };
    while (i < n)
    {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r')
        {
            ++i;
        }
        else if (c == '\n' || c == ';')
        {
            push(Tok::Newline);
            if (c == '\n')
            {
                ++line;
            }
            ++i;
        }
        else if (c == '#')
        {
            while (i < n && s[i] != '\n')
            {
                ++i;
            }
        }
        else if (IsDigit(c))
        {
            usize j = i;
            bool is_float = false;
            while (j < n && IsDigit(s[j]))
            {
                ++j;
            }
            if (j + 1 < n && s[j] == '.' && IsDigit(s[j + 1]))
            {
                is_float = true;
                ++j;
                while (j < n && IsDigit(s[j]))
                {
                    ++j;
                }
            }
            Token t;
            t.kind = Tok::Number;
            t.line = line;
            t.is_float = is_float;
            if (is_float)
            {
                const ParseDoubleResult r = ParseDoubleExact(s.Data() + i, s.Data() + j);
                t.float_value = r.value;
            }
            else
            {
                u64 v = 0;
                for (usize k = i; k < j; ++k)
                {
                    v = v * 10 + static_cast<u64>(s[k] - '0');
                }
                t.int_value = static_cast<i64>(v);
            }
            tokens_.Push(t);
            i = j;
        }
        else if (IsIdentStart(c))
        {
            usize j = i;
            while (j < n && (IsIdentStart(s[j]) || IsDigit(s[j])))
            {
                ++j;
            }
            Token t;
            t.text = StringView(s.Data() + i, j - i);
            t.kind = KeywordKind(t.text);
            t.line = line;
            tokens_.Push(t);
            i = j;
        }
        else if (c == '"')
        {
            Text value = MakeText(StringView());
            ++i;
            bool closed = false;
            while (i < n)
            {
                if (s[i] == '"')
                {
                    closed = true;
                    ++i;
                    break;
                }
                if (s[i] == '\\' && i + 1 < n)
                {
                    ++i;
                    value.Append(s[i] == 'n' ? '\n' : s[i]);
                }
                else
                {
                    value.Append(s[i]);
                }
                ++i;
            }
            Token t;
            t.kind = closed ? Tok::String : Tok::End;
            t.line = line;
            t.string_index = static_cast<u32>(strings_.Size());
            strings_.Push(Move(value));
            tokens_.Push(t);
            if (!closed)
            {
                failed_ = true;
                error_ = MakeText("unfinished string");
                error_line_ = line;
                return;
            }
        }
        else
        {
            Tok kind = Tok::End;
            usize len = 1;
            const char next = i + 1 < n ? s[i + 1] : '\0';
            switch (c)
            {
            case '(': kind = Tok::LParen; break;
            case ')': kind = Tok::RParen; break;
            case ',': kind = Tok::Comma; break;
            case '+': kind = Tok::Plus; break;
            case '-': kind = Tok::Minus; break;
            case '*': kind = Tok::Star; break;
            case '/': kind = Tok::Slash; break;
            case '%': kind = Tok::Percent; break;
            case '=':
                kind = next == '=' ? Tok::EqEq : Tok::Assign;
                len = next == '=' ? 2 : 1;
                break;
            case '!':
                kind = next == '=' ? Tok::NotEq : Tok::End;
                len = 2;
                break;
            case '<':
                kind = next == '=' ? Tok::LessEq : Tok::Less;
                len = next == '=' ? 2 : 1;
                break;
            case '>':
                kind = next == '=' ? Tok::GreaterEq : Tok::Greater;
                len = next == '=' ? 2 : 1;
                break;
            default: break;
            }
            if (kind == Tok::End)
            {
                failed_ = true;
                error_ = MakeText("unexpected character");
                error_line_ = line;
                return;
            }
            push(kind);
            i += len;
        }
    }
    push(Tok::Newline);
    push(Tok::End);
}

usize Compiler::Emit(Op op, i32 a, i32 b)
{
    Instr in;
    in.op = op;
    in.a = a;
    in.b = b;
    in.line = Peek().line;
    fn_->code.Push(in);
    return fn_->code.Size() - 1;
}

i32 Compiler::AddConst(Value v)
{
    fn_->consts.Push(Move(v));
    return static_cast<i32>(fn_->consts.Size() - 1);
}

i32 Compiler::AddName(StringView name)
{
    for (usize i = 0; i < fn_->names.Size(); ++i)
    {
        if (fn_->names[i].View() == name)
        {
            return static_cast<i32>(i);
        }
    }
    fn_->names.Push(MakeText(name));
    return static_cast<i32>(fn_->names.Size() - 1);
}

i32 Compiler::FindLocal(StringView name) const
{
    for (usize i = 0; i < local_names_.Size(); ++i)
    {
        if (local_names_[i].View() == name)
        {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

bool Compiler::Compile(Function* main)
{
    if (failed_)
    {
        return false;
    }
    fn_ = main;
    main->name = MakeText("main chunk");
    main->chunk = MakeText(chunk_.View());
    Block();
    if (!failed_ && !At(Tok::End))
    {
        Fail("unexpected 'end' or 'else'");
    }
    Emit(Op::Return, 0);
    return !failed_;
}

void Compiler::Block()
{
    DepthGuard guard(this);
    if (guard.TooDeep())
    {
        return;
    }
    for (;;)
    {
        SkipNewlines();
        if (Failed() || At(Tok::End) || At(Tok::KwEnd) || At(Tok::KwElse))
        {
            return;
        }
        Statement();
        if (Failed())
        {
            return;
        }
        if (!At(Tok::Newline) && !At(Tok::End) && !At(Tok::KwEnd) && !At(Tok::KwElse))
        {
            Fail("unexpected symbol after a statement");
            return;
        }
    }
}

void Compiler::Statement()
{
    switch (Peek().kind)
    {
    case Tok::KwDef: DefStatement(); return;
    case Tok::KwIf: IfStatement(); return;
    case Tok::KwWhile: WhileStatement(); return;
    case Tok::KwReturn: ReturnStatement(); return;
    case Tok::KwGlobal:
    {
        Take();
        if (!At(Tok::Ident))
        {
            Fail("a name is expected after 'global'");
            return;
        }
        const StringView name = Take().text;
        if (!Accept(Tok::Assign))
        {
            Fail("'=' is expected");
            return;
        }
        Expression();
        if (!Failed())
        {
            Emit(Op::StoreGlobal, AddName(name));
        }
        return;
    }
    case Tok::Ident:
        if (tokens_[pos_ + 1].kind == Tok::Assign)
        {
            const StringView name = Take().text;
            Take();
            Assign(name, false);
            return;
        }
        break;
    default: break;
    }
    Expression();
    if (!Failed())
    {
        Emit(Op::Pop);
    }
}

void Compiler::Assign(StringView name, bool force_global)
{
    Expression();
    if (Failed())
    {
        return;
    }
    if (in_function_ && !force_global)
    {
        i32 slot = FindLocal(name);
        if (slot < 0)
        {
            local_names_.Push(MakeText(name));
            slot = static_cast<i32>(local_names_.Size() - 1);
        }
        Emit(Op::StoreLocal, slot);
    }
    else
    {
        Emit(Op::StoreGlobal, AddName(name));
    }
}

void Compiler::DefStatement()
{
    Take();
    if (in_function_)
    {
        Fail("a function cannot be defined inside a function");
        return;
    }
    if (!At(Tok::Ident))
    {
        Fail("a function name is expected");
        return;
    }
    UniquePtr<Function> fn = MakeUnique<Function>();
    fn->name = MakeText(Take().text);
    fn->chunk = MakeText(chunk_.View());
    if (!Accept(Tok::LParen))
    {
        Fail("'(' is expected");
        return;
    }
    local_names_.Clear();
    if (!At(Tok::RParen))
    {
        do
        {
            if (!At(Tok::Ident))
            {
                Fail("a parameter name is expected");
                return;
            }
            local_names_.Push(MakeText(Take().text));
        } while (Accept(Tok::Comma));
    }
    if (!Accept(Tok::RParen))
    {
        Fail("')' is expected");
        return;
    }
    fn->params = static_cast<u32>(local_names_.Size());
    Function* outer = fn_;
    fn_ = fn.Get();
    in_function_ = true;
    Block();
    if (!Failed() && !Accept(Tok::KwEnd))
    {
        Fail("'end' is expected to close the function");
    }
    Emit(Op::Return, 0);
    fn->locals = static_cast<u32>(local_names_.Size());
    in_function_ = false;
    fn_ = outer;
    local_names_.Clear();
    if (!Failed())
    {
        state_.DefineFunction(Move(fn));
    }
}

void Compiler::IfStatement()
{
    Take();
    Expression();
    if (Failed())
    {
        return;
    }
    const usize to_else = Emit(Op::JumpIfFalse);
    Block();
    if (Failed())
    {
        return;
    }
    if (Accept(Tok::KwElse))
    {
        const usize to_end = Emit(Op::Jump);
        Patch(to_else);
        Block();
        if (Failed())
        {
            return;
        }
        Patch(to_end);
    }
    else
    {
        Patch(to_else);
    }
    if (!Accept(Tok::KwEnd))
    {
        Fail("'end' is expected to close the 'if'");
    }
}

void Compiler::WhileStatement()
{
    Take();
    const i32 top = static_cast<i32>(fn_->code.Size());
    Expression();
    if (Failed())
    {
        return;
    }
    const usize to_exit = Emit(Op::JumpIfFalse);
    Block();
    if (Failed())
    {
        return;
    }
    Emit(Op::Jump, top);
    Patch(to_exit);
    if (!Accept(Tok::KwEnd))
    {
        Fail("'end' is expected to close the 'while'");
    }
}

void Compiler::ReturnStatement()
{
    Take();
    i32 count = 0;
    if (!At(Tok::Newline) && !At(Tok::End) && !At(Tok::KwEnd) && !At(Tok::KwElse))
    {
        do
        {
            Expression();
            ++count;
        } while (!Failed() && Accept(Tok::Comma));
    }
    if (!Failed())
    {
        Emit(Op::Return, count);
    }
}

void Compiler::OrExpr()
{
    AndExpr();
    while (!Failed() && Accept(Tok::KwOr))
    {
        const usize skip = Emit(Op::JumpIfTrueKeep);
        AndExpr();
        Patch(skip);
    }
}

void Compiler::AndExpr()
{
    Comparison();
    while (!Failed() && Accept(Tok::KwAnd))
    {
        const usize skip = Emit(Op::JumpIfFalseKeep);
        Comparison();
        Patch(skip);
    }
}

void Compiler::Comparison()
{
    Additive();
    for (;;)
    {
        Op op;
        switch (Peek().kind)
        {
        case Tok::EqEq: op = Op::Eq; break;
        case Tok::NotEq: op = Op::Ne; break;
        case Tok::Less: op = Op::Lt; break;
        case Tok::LessEq: op = Op::Le; break;
        case Tok::Greater: op = Op::Gt; break;
        case Tok::GreaterEq: op = Op::Ge; break;
        default: return;
        }
        Take();
        Additive();
        if (Failed())
        {
            return;
        }
        Emit(op);
    }
}

void Compiler::Additive()
{
    Multiplicative();
    while (!Failed() && (At(Tok::Plus) || At(Tok::Minus)))
    {
        const Op op = Take().kind == Tok::Plus ? Op::Add : Op::Sub;
        Multiplicative();
        if (!Failed())
        {
            Emit(op);
        }
    }
}

void Compiler::Multiplicative()
{
    Unary();
    while (!Failed() && (At(Tok::Star) || At(Tok::Slash) || At(Tok::Percent)))
    {
        const Tok kind = Take().kind;
        Unary();
        if (!Failed())
        {
            Emit(kind == Tok::Star ? Op::Mul : (kind == Tok::Slash ? Op::Div : Op::Mod));
        }
    }
}

void Compiler::Unary()
{
    DepthGuard guard(this);
    if (guard.TooDeep())
    {
        return;
    }
    if (Accept(Tok::Minus))
    {
        Unary();
        if (!Failed())
        {
            Emit(Op::Neg);
        }
    }
    else if (Accept(Tok::KwNot))
    {
        Unary();
        if (!Failed())
        {
            Emit(Op::Not);
        }
    }
    else
    {
        Primary();
    }
}

void Compiler::Primary()
{
    const Token t = Take();
    switch (t.kind)
    {
    case Tok::Number:
        Emit(Op::Const, AddConst(t.is_float ? Value::FromFloat(t.float_value) : Value::FromInt(t.int_value)));
        return;
    case Tok::String: Emit(Op::Const, AddConst(Value::FromText(strings_[t.string_index].View()))); return;
    case Tok::KwTrue: Emit(Op::True); return;
    case Tok::KwFalse: Emit(Op::False); return;
    case Tok::KwNil: Emit(Op::Nil); return;
    case Tok::LParen:
        Expression();
        if (!Failed() && !Accept(Tok::RParen))
        {
            Fail("')' is expected");
        }
        return;
    case Tok::Ident:
        if (Accept(Tok::LParen))
        {
            i32 argc = 0;
            if (!At(Tok::RParen))
            {
                do
                {
                    Expression();
                    ++argc;
                } while (!Failed() && Accept(Tok::Comma));
            }
            if (!Failed() && !Accept(Tok::RParen))
            {
                Fail("')' is expected after the arguments");
            }
            if (!Failed())
            {
                Emit(Op::Call, argc, AddName(t.text));
            }
        }
        else
        {
            const i32 slot = in_function_ ? FindLocal(t.text) : -1;
            if (slot >= 0)
            {
                Emit(Op::LoadLocal, slot);
            }
            else
            {
                Emit(Op::LoadGlobal, AddName(t.text));
            }
        }
        return;
    default:
        pos_ = pos_ > 0 ? pos_ - 1 : 0;
        Fail("unexpected symbol in an expression");
        return;
    }
}

// ---- state implementation

const Function* ExprState::FindFunction(StringView name) const
{
    for (usize i = 0; i < names_.Size(); ++i)
    {
        if (names_[i]->name.View() == name)
        {
            return names_[i];
        }
    }
    return nullptr;
}

const Native* ExprState::FindNative(StringView name) const
{
    for (usize i = 0; i < natives_.Size(); ++i)
    {
        if (natives_[i].name.View() == name)
        {
            return &natives_[i];
        }
    }
    return nullptr;
}

Global* ExprState::FindGlobal(StringView name)
{
    for (usize i = 0; i < globals_.Size(); ++i)
    {
        if (globals_[i].name.View() == name)
        {
            return &globals_[i];
        }
    }
    return nullptr;
}

Global& ExprState::GlobalFor(StringView name)
{
    Global* found = FindGlobal(name);
    if (found != nullptr)
    {
        return *found;
    }
    Global g;
    g.name = MakeText(name);
    globals_.Push(Move(g));
    return globals_[globals_.Size() - 1];
}

Capabilities ExprState::DoCapabilities() const
{
    Capabilities caps;
    caps.coroutines = true;
    caps.instruction_budget = true;
    caps.time_budget = true;
    return caps;
}

Outcome ExprState::Fail(Status status, const Coroutine& co, StringView message)
{
    Text text = MakeText(StringView());
    Text trace = MakeText(StringView());
    char number[16];
    for (usize i = co.frames.Size(); i-- > 0;)
    {
        const Frame& f = co.frames[i];
        const usize pc = f.pc > 0 ? f.pc - 1 : 0;
        const u32 line = pc < f.fn->code.Size() ? f.fn->code[pc].line : 0;
        FormatTo(number, sizeof number, "%u", line);
        Text where = MakeText(f.fn->chunk.View());
        where.Append(':');
        where.Append(StringView(number, CStrLength(number)));
        if (i + 1 == co.frames.Size())
        {
            text = MakeText(where.View());
            text.Append(": ");
            text.Append(message);
        }
        trace.Append(where.View());
        trace.Append(" in ");
        trace.Append(f.fn->name.View());
        trace.Append('\n');
    }
    if (co.frames.Size() == 0)
    {
        text = MakeText(message);
    }
    Outcome out(status, text.View());
    out.trace = Move(trace);
    return out;
}

Outcome ExprState::DoLoad(StringView chunk_name, StringView source)
{
    if (closed_)
    {
        return Outcome(Status::Invalid, "the state is closed");
    }
    if (chunk_name.Length() == 0)
    {
        return Outcome(Status::Invalid, "empty chunk name");
    }
    UniquePtr<Function> main = MakeUnique<Function>();
    Compiler compiler(*this, chunk_name, source);
    if (!compiler.Compile(main.Get()))
    {
        char number[16];
        FormatTo(number, sizeof number, "%u", compiler.ErrorLine());
        Text message = MakeText(chunk_name);
        message.Append(':');
        message.Append(StringView(number, CStrLength(number)));
        message.Append(": ");
        message.Append(compiler.Error().View());
        Outcome out(Status::SyntaxError, message.View());
        out.trace = MakeText(StringView(message.CStr(), message.Length()));
        return out;
    }
    const Function* fn = main.Get();
    functions_.Push(Move(main));
    UniquePtr<Coroutine> co = MakeUnique<Coroutine>();
    co->id = 0;
    Frame frame;
    frame.fn = fn;
    co->frames.Push(frame);
    Budget budget;
    budget.instructions = kLoadInstructionBudget;
    CallOutput out;
    Outcome failure;
    const RunResult r = Run(co.Get(), budget, &out.results, &out.wait, &failure);
    if (r == RunResult::Failed)
    {
        return failure;
    }
    if (r == RunResult::Yielded)
    {
        return Outcome(Status::RuntimeError, "a chunk cannot wait while it loads");
    }
    return Outcome();
}

Outcome ExprState::DoRegisterFunction(StringView name, NativeFunction function, void* user)
{
    if (name.Length() == 0 || function == nullptr)
    {
        return Outcome(Status::Invalid, "a native function needs a name and a function");
    }
    for (usize i = 0; i < natives_.Size(); ++i)
    {
        if (natives_[i].name.View() == name)
        {
            natives_[i].function = function;
            natives_[i].user = user;
            return Outcome();
        }
    }
    Native n;
    n.name = MakeText(name);
    n.function = function;
    n.user = user;
    natives_.Push(Move(n));
    return Outcome();
}

usize ExprState::DoMemoryUsed()
{
    usize bytes = 512;
    for (usize i = 0; i < functions_.Size(); ++i)
    {
        bytes += sizeof(Function) + functions_[i]->code.Size() * sizeof(Instr) +
                 functions_[i]->consts.Size() * sizeof(Value);
    }
    bytes += globals_.Size() * sizeof(Global);
    for (usize i = 0; i < coroutines_.Size(); ++i)
    {
        bytes += sizeof(Coroutine) + coroutines_[i]->stack.Size() * sizeof(Value) +
                 coroutines_[i]->frames.Size() * sizeof(Frame);
    }
    return bytes;
}

void ExprState::DoClose()
{
    coroutines_ = Vector<UniquePtr<Coroutine>>();
    globals_ = Vector<Global>();
    natives_ = Vector<Native>();
    names_ = Vector<const Function*>();
    functions_ = Vector<UniquePtr<Function>>();
    closed_ = true;
}

void ExprState::DoAbandon(u64 coroutine)
{
    for (usize i = 0; i < coroutines_.Size(); ++i)
    {
        if (coroutines_[i]->id == coroutine)
        {
            coroutines_.Remove(i);
            return;
        }
    }
}

Outcome ExprState::StartAndRun(const Function* fn, Span<const Value> args, const Budget& budget, CallOutput* out)
{
    UniquePtr<Coroutine> co = MakeUnique<Coroutine>();
    co->id = next_coroutine_++;
    for (usize i = 0; i < fn->params; ++i)
    {
        co->stack.Push(i < args.Size() ? Value(args[i]) : Value());
    }
    while (co->stack.Size() < fn->locals)
    {
        co->stack.Push(Value());
    }
    Frame frame;
    frame.fn = fn;
    frame.base = 0;
    co->frames.Push(frame);
    Outcome failure;
    const RunResult r = Run(co.Get(), budget, &out->results, &out->wait, &failure);
    return FinishRun(r, Move(co), out, Move(failure));
}

Outcome ExprState::FinishRun(RunResult result, UniquePtr<Coroutine> co, CallOutput* out, Outcome failure)
{
    if (result == RunResult::Failed)
    {
        out->results.Clear();
        return failure;
    }
    if (result == RunResult::Yielded)
    {
        out->coroutine = co->id;
        coroutines_.Push(Move(co));
    }
    return Outcome();
}

Outcome ExprState::DoCall(StringView path, Span<const Value> args, const Budget& budget, CallOutput* out)
{
    if (closed_)
    {
        return Outcome(Status::Invalid, "the state is closed");
    }
    if (path.Length() == 0)
    {
        return Outcome(Status::Invalid, "empty function path");
    }
    if (budget.memory_bytes != 0)
    {
        return Outcome(Status::Unsupported, "script_expr does not count memory");
    }
    for (usize i = 0; i < args.Size(); ++i)
    {
        if (args[i].Check(ValueLimits()) != ValueCheck::Ok)
        {
            return Outcome(Status::Invalid, "an argument is over the value limits");
        }
    }
    const Function* fn = FindFunction(path);
    if (fn != nullptr)
    {
        return StartAndRun(fn, args, budget, out);
    }
    const Native* native = FindNative(path);
    if (native != nullptr)
    {
        return native->function(native->user, args, &out->results);
    }
    Text message = MakeText("attempt to call a nil value (global '");
    message.Append(path);
    message.Append("')");
    return Outcome(Status::RuntimeError, message.View());
}

Outcome ExprState::DoResume(u64 coroutine, Span<const Value> values, const Budget& budget, CallOutput* out)
{
    if (closed_)
    {
        return Outcome(Status::Invalid, "the state is closed");
    }
    if (budget.memory_bytes != 0)
    {
        return Outcome(Status::Unsupported, "script_expr does not count memory");
    }
    for (usize i = 0; i < coroutines_.Size(); ++i)
    {
        if (coroutine != 0 && coroutines_[i]->id == coroutine)
        {
            UniquePtr<Coroutine> co = Move(coroutines_[i]);
            coroutines_.Remove(i);
            co->stack.Push(values.Size() > 0 ? Value(values[0]) : Value());
            Outcome failure;
            const RunResult r = Run(co.Get(), budget, &out->results, &out->wait, &failure);
            return FinishRun(r, Move(co), out, Move(failure));
        }
    }
    return Outcome(Status::Invalid, "that call is not suspended in this state");
}

// ---- the machine

ExprState::RunResult ExprState::Run(Coroutine* co, const Budget& budget, Vector<Value>* results, WaitRequest* wait,
                                    Outcome* failure)
{
    Vector<Value>& stack = co->stack;
    u64 instructions_left = budget.instructions;
    const bool limited = budget.instructions != 0;
    const u64 deadline = budget.nanoseconds != 0 ? os::MonotonicTimeNanos() + budget.nanoseconds : 0;
    u32 tick = 0;

#define EXPR_FAIL(status, message)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        *failure = Fail(status, *co, StringView(message));                                                             \
        return RunResult::Failed;                                                                                      \
    } while (false)

    for (;;)
    {
        Frame& frame = co->frames[co->frames.Size() - 1];
        if (frame.pc >= frame.fn->code.Size())
        {
            EXPR_FAIL(Status::RuntimeError, "the function ran off its end");
        }
        const Instr in = frame.fn->code[frame.pc++];
        if (limited)
        {
            if (instructions_left == 0)
            {
                *failure = Fail(Status::BudgetExceeded, *co, StringView("instruction budget exceeded"));
                return RunResult::Failed;
            }
            --instructions_left;
        }
        if (deadline != 0 && (++tick & 63u) == 0 && os::MonotonicTimeNanos() > deadline)
        {
            *failure = Fail(Status::BudgetExceeded, *co, StringView("time budget exceeded"));
            return RunResult::Failed;
        }
        if (stack.Size() + 4 > kMaxStack)
        {
            EXPR_FAIL(Status::RuntimeError, "stack overflow");
        }
        switch (in.op)
        {
        case Op::Const: stack.Push(Value(frame.fn->consts[static_cast<usize>(in.a)])); break;
        case Op::Nil: stack.Push(Value()); break;
        case Op::True: stack.Push(Value::FromBool(true)); break;
        case Op::False: stack.Push(Value::FromBool(false)); break;
        case Op::LoadLocal: stack.Push(Value(stack[frame.base + static_cast<usize>(in.a)])); break;
        case Op::StoreLocal:
        {
            Value v = Move(stack[stack.Size() - 1]);
            stack.Pop();
            stack[frame.base + static_cast<usize>(in.a)] = Move(v);
            break;
        }
        case Op::LoadGlobal:
        {
            Global* g = FindGlobal(frame.fn->names[static_cast<usize>(in.a)].View());
            stack.Push(g != nullptr ? Value(g->value) : Value());
            break;
        }
        case Op::StoreGlobal:
        {
            Value v = Move(stack[stack.Size() - 1]);
            stack.Pop();
            GlobalFor(frame.fn->names[static_cast<usize>(in.a)].View()).value = Move(v);
            break;
        }
        case Op::Neg:
        {
            Value& top = stack[stack.Size() - 1];
            if (top.Kind() == ValueKind::Int)
            {
                top = Value::FromInt(static_cast<i64>(0 - static_cast<u64>(top.AsInt())));
            }
            else if (top.Kind() == ValueKind::Float)
            {
                top = Value::FromFloat(-top.AsFloat());
            }
            else
            {
                EXPR_FAIL(Status::RuntimeError, "attempt to negate a non-number value");
            }
            break;
        }
        case Op::Not:
        {
            const bool t = Truthy(stack[stack.Size() - 1]);
            stack[stack.Size() - 1] = Value::FromBool(!t);
            break;
        }
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Div:
        case Op::Mod:
        {
            Value rhs = Move(stack[stack.Size() - 1]);
            stack.Pop();
            Value& lhs = stack[stack.Size() - 1];
            if (in.op == Op::Add && lhs.Kind() == ValueKind::Text && rhs.Kind() == ValueKind::Text)
            {
                if (lhs.Text().Length() + rhs.Text().Length() > kMaxStringBytes)
                {
                    EXPR_FAIL(Status::RuntimeError, "string too long");
                }
                Text joined = MakeText(lhs.Text());
                joined.Append(rhs.Text());
                lhs = Value::FromText(joined.View());
                break;
            }
            if (!lhs.IsNumber() || !rhs.IsNumber())
            {
                Text message = MakeText("attempt to perform arithmetic on a ");
                message.Append(StringView(KindName(lhs.IsNumber() ? rhs : lhs)));
                message.Append(" value");
                *failure = Fail(Status::RuntimeError, *co, message.View());
                return RunResult::Failed;
            }
            if (in.op == Op::Mod)
            {
                if (lhs.Kind() != ValueKind::Int || rhs.Kind() != ValueKind::Int)
                {
                    EXPR_FAIL(Status::RuntimeError, "'%' needs two integers");
                }
                if (rhs.AsInt() == 0)
                {
                    EXPR_FAIL(Status::RuntimeError, "modulo by zero");
                }
                lhs = Value::FromInt(rhs.AsInt() == -1 ? 0 : lhs.AsInt() % rhs.AsInt());
            }
            else if (in.op == Op::Div)
            {
                lhs = Value::FromFloat(lhs.ToDouble() / rhs.ToDouble());
            }
            else if (lhs.Kind() == ValueKind::Int && rhs.Kind() == ValueKind::Int)
            {
                const u64 a = static_cast<u64>(lhs.AsInt());
                const u64 b = static_cast<u64>(rhs.AsInt());
                lhs = Value::FromInt(static_cast<i64>(in.op == Op::Add ? a + b : (in.op == Op::Sub ? a - b : a * b)));
            }
            else
            {
                const f64 a = lhs.ToDouble();
                const f64 b = rhs.ToDouble();
                lhs = Value::FromFloat(in.op == Op::Add ? a + b : (in.op == Op::Sub ? a - b : a * b));
            }
            break;
        }
        case Op::Eq:
        case Op::Ne:
        {
            Value rhs = Move(stack[stack.Size() - 1]);
            stack.Pop();
            Value& lhs = stack[stack.Size() - 1];
            const bool same = (lhs.IsNumber() && rhs.IsNumber()) ? lhs.ToDouble() == rhs.ToDouble() : lhs.Equals(rhs);
            lhs = Value::FromBool(in.op == Op::Eq ? same : !same);
            break;
        }
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge:
        {
            Value rhs = Move(stack[stack.Size() - 1]);
            stack.Pop();
            Value& lhs = stack[stack.Size() - 1];
            int order = 0;
            if (lhs.IsNumber() && rhs.IsNumber())
            {
                const f64 a = lhs.ToDouble();
                const f64 b = rhs.ToDouble();
                order = a < b ? -1 : (a > b ? 1 : 0);
                if (a != a || b != b)
                {
                    lhs = Value::FromBool(false);
                    break;
                }
            }
            else if (lhs.Kind() == ValueKind::Text && rhs.Kind() == ValueKind::Text)
            {
                const usize n = lhs.Text().Length() < rhs.Text().Length() ? lhs.Text().Length() : rhs.Text().Length();
                order = MemCompare(lhs.Text().Data(), rhs.Text().Data(), n);
                if (order == 0)
                {
                    order = lhs.Text().Length() < rhs.Text().Length()
                                ? -1
                                : (lhs.Text().Length() > rhs.Text().Length() ? 1 : 0);
                }
            }
            else
            {
                EXPR_FAIL(Status::RuntimeError, "attempt to compare incompatible values");
            }
            const bool result = in.op == Op::Lt
                                    ? order < 0
                                    : (in.op == Op::Le ? order <= 0 : (in.op == Op::Gt ? order > 0 : order >= 0));
            lhs = Value::FromBool(result);
            break;
        }
        case Op::Jump: frame.pc = static_cast<usize>(in.a); break;
        case Op::JumpIfFalse:
        {
            const bool t = Truthy(stack[stack.Size() - 1]);
            stack.Pop();
            if (!t)
            {
                frame.pc = static_cast<usize>(in.a);
            }
            break;
        }
        case Op::JumpIfFalseKeep:
            if (!Truthy(stack[stack.Size() - 1]))
            {
                frame.pc = static_cast<usize>(in.a);
            }
            else
            {
                stack.Pop();
            }
            break;
        case Op::JumpIfTrueKeep:
            if (Truthy(stack[stack.Size() - 1]))
            {
                frame.pc = static_cast<usize>(in.a);
            }
            else
            {
                stack.Pop();
            }
            break;
        case Op::Pop: stack.Pop(); break;
        case Op::Call:
        {
            const usize argc = static_cast<usize>(in.a);
            const StringView name = frame.fn->names[static_cast<usize>(in.b)].View();
            const usize args_at = stack.Size() - argc;
            const bool is_seconds = name == StringView("wait");
            const bool is_frames = name == StringView("wait_frames");
            const bool is_event = name == StringView("wait_event");
            if (is_seconds || is_frames || is_event)
            {
                if (argc != 1 || co->id == 0)
                {
                    EXPR_FAIL(Status::RuntimeError,
                              co->id == 0 ? "a chunk cannot wait while it loads" : "a wait takes one argument");
                }
                const Value arg = stack[args_at];
                stack.Pop();
                wait->kind = is_seconds ? WaitRequest::Kind::Seconds
                                        : (is_frames ? WaitRequest::Kind::Frames : WaitRequest::Kind::Event);
                if (is_event)
                {
                    if (arg.Kind() != ValueKind::Text)
                    {
                        EXPR_FAIL(Status::RuntimeError, "wait_event needs an event name");
                    }
                    wait->event = MakeText(arg.Text());
                }
                else
                {
                    if (!arg.IsNumber() || !(arg.ToDouble() >= 0.0) ||
                        arg.ToDouble() > (is_frames ? 4294967295.0 : 1.0e12))
                    {
                        EXPR_FAIL(Status::RuntimeError, "a wait needs a finite number in range that is not negative");
                    }
                    wait->seconds = is_seconds ? arg.ToDouble() : 0.0;
                    wait->frames = is_frames ? static_cast<u32>(arg.ToDouble()) : 0;
                }
                return RunResult::Yielded;
            }
            const Function* callee = FindFunction(name);
            if (callee != nullptr)
            {
                if (co->frames.Size() >= kMaxFrames)
                {
                    EXPR_FAIL(Status::RuntimeError, "stack overflow");
                }
                // Fit the arguments to the parameters: missing ones are nil, extra ones are dropped; then room for the
                // locals.
                while (stack.Size() - args_at < callee->params)
                {
                    stack.Push(Value());
                }
                while (stack.Size() - args_at > callee->params)
                {
                    stack.Pop();
                }
                while (stack.Size() - args_at < callee->locals)
                {
                    stack.Push(Value());
                }
                Frame next;
                next.fn = callee;
                next.base = args_at;
                co->frames.Push(next);
                break;
            }
            const Native* native = FindNative(name);
            if (native != nullptr)
            {
                Vector<Value> call_args;
                for (usize i = 0; i < argc; ++i)
                {
                    call_args.Push(Move(stack[args_at + i]));
                }
                while (stack.Size() > args_at)
                {
                    stack.Pop();
                }
                Vector<Value> native_results;
                Outcome native_out = native->function(
                    native->user, Span<const Value>(call_args.Data(), call_args.Size()), &native_results);
                if (!native_out.IsOk())
                {
                    *failure = Fail(native_out.status, *co, native_out.message.View());
                    return RunResult::Failed;
                }
                stack.Push(native_results.Size() > 0 ? Move(native_results[0]) : Value());
                break;
            }
            Text message = MakeText("attempt to call a nil value (global '");
            message.Append(name);
            message.Append("')");
            *failure = Fail(Status::RuntimeError, *co, message.View());
            return RunResult::Failed;
        }
        case Op::Return:
        {
            const usize n = static_cast<usize>(in.a);
            const usize first = stack.Size() - n;
            if (co->frames.Size() == 1)
            {
                for (usize i = 0; i < n; ++i)
                {
                    results->Push(Move(stack[first + i]));
                }
                return RunResult::Done;
            }
            Value result = n > 0 ? Move(stack[first]) : Value();
            const usize base = frame.base;
            while (stack.Size() > base)
            {
                stack.Pop();
            }
            co->frames.Pop();
            stack.Push(Move(result));
            break;
        }
        }
    }
#undef EXPR_FAIL
}

// ---------------------------------------------------------------- backend

class ExprBackend : public Backend
{
public:
    StringView Name() const override { return StringView("expr"); }
    StringView Version() const override { return StringView("1"); }
    Capabilities Caps() const override
    {
        Capabilities caps;
        caps.coroutines = true;
        caps.instruction_budget = true;
        caps.time_budget = true;
        return caps;
    }
    UniquePtr<State> CreateState(u64) override { return MakeUnique<ExprState>(); }
};

UniquePtr<Backend> MakeExprBackend()
{
    return MakeUnique<ExprBackend>();
}

}  // namespace

UniquePtr<Backend> CreateExprBackend()
{
    return MakeExprBackend();
}

bool RegisterExprBackend()
{
    BackendDescriptor descriptor;
    descriptor.name = "expr";
    descriptor.create = &MakeExprBackend;
    return RegisterBackend(descriptor);
}

}  // namespace engine_placeholder::script::expr
