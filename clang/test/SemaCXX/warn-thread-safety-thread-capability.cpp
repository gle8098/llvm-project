// RUN: %clang_cc1 -fsyntax-only -verify -std=c++17 -Wthread-safety -Wthread-safety-ste -fcxx-exceptions %s

//=============================================================================

#define THREAD_CAPABILITY __attribute__((capability("thread")))

#define GUARDED_BY_THREAD(x) __attribute__((guarded_by(x)))

#define PT_GUARDED_BY_THREAD(x) __attribute__((pt_guarded_by(x)))

#define REQUIRES_THREAD(...) __attribute__((requires_capability(__VA_ARGS__)))

#define EXCLUDES_THREAD(...) __attribute__((locks_excluded(__VA_ARGS__)))
#define ASSERT_IN_THREAD(x) __attribute__((assert_capability(x)))

#define EXECUTE_IN_THREAD __attribute__((ste_exec))

//=============================================================================

namespace std {

// std::function without implementation

template <typename T>
class function;

template <typename R, typename... Args>
class function<R(Args...)>
{
    typedef R (*invoke_fn_t)();

    invoke_fn_t invoke_f = nullptr;
public:
    function() {}

    // construct from any functor type
    template <typename Functor>
    function(Functor f) {}

    // copy constructor
    function(function const& rhs) {}

    R operator()(Args&&... args)
    {
        return this->invoke_f();
    }
};

}

//=============================================================================

class TaskCallerContext {
public:
  int dummyValue = 1;
};

class PeriodicTask {
public:
  int dummyValue = 2;
};

class THREAD_CAPABILITY ThreadExecutor
{
public:
    virtual ~ThreadExecutor() = default;

    static ThreadExecutor* makeSingleThreadExecutor() {
      static ThreadExecutor* (*factory)();
      return factory();
    }

    using TaskBody = std::function<void()>;

    virtual void assertInThread() const noexcept ASSERT_IN_THREAD() = 0;

    inline bool exec(TaskBody taskBody, TaskCallerContext taskCallerContext = {}) EXECUTE_IN_THREAD
    {
        return execImpl(taskBody, taskCallerContext);
    };

    virtual PeriodicTask execPeriodicTask(TaskBody taskBody, int period) EXECUTE_IN_THREAD = 0;

protected:
    /**
     * see bool exec(TaskBody taskBody, std::optional<TaskCallerContext> taskCallerContext = {})
     */
    virtual bool execImpl(TaskBody taskBody, TaskCallerContext taskCallerContext) = 0;
};

//=============================================================================

class SomeClass {
public:
    SomeClass();

    void increase();
    void decrease();
    void touchCounter(); ///< calls increaseImpl, then decreases counter & invokes callback
    void touchCounter2();

    int get() const;
    std::function<void()> getIncreaseImpl();

private:
    void init();
    void startImpl() REQUIRES_THREAD(*singleThreadExecutor);

    void increaseImpl() REQUIRES_THREAD(*singleThreadExecutor);
    void divideCounterByTenImpl();

    void onCounterChanged();

    ThreadExecutor* singleThreadExecutor = nullptr;
    ThreadExecutor* callbackExecutor;

    PeriodicTask periodicCounterIncrease GUARDED_BY_THREAD(*singleThreadExecutor);
    int counter GUARDED_BY_THREAD(*singleThreadExecutor) = 0;

    std::function<void()> counterChanged GUARDED_BY_THREAD(*callbackExecutor);
};

//=============================================================================

namespace std {

namespace impl {

template<class Fn, class ... Args>
class binder {};

template<>
class binder<decltype(&SomeClass::increaseImpl), SomeClass*>
{
public:
  template<class TFn, class ... TArgs>
  explicit constexpr binder(TFn&& f, TArgs&&... args) noexcept {}
  
  template<class ... CallArgs>
  constexpr void operator()(CallArgs&&... args) {}
};
}

template<class Fn, class ... Args>
decltype(auto) bind(Fn&& f, Args&&... args)
{
  return impl::binder<Fn, Args...>{f, args...};
}

}

//=============================================================================

SomeClass::SomeClass() {
    singleThreadExecutor = ThreadExecutor::makeSingleThreadExecutor();
    callbackExecutor = ThreadExecutor::makeSingleThreadExecutor();
    init();
}

void SomeClass::init() {
    callbackExecutor->exec([this] { startImpl(); }); // expected-warning {{calling 'exec' acquires 'callbackExecutor' capability, but argument is associated with 'singleThreadExecutor' capability}} \
                                                     // expected-note {{lambda implicitly requires thread 'singleThreadExecutor'}} \
                                                     // expected-note {{... the thread was required for this statement}}
}

void SomeClass::startImpl() {
    counter = 1;
    periodicCounterIncrease = singleThreadExecutor->execPeriodicTask(std::bind(&SomeClass::increaseImpl, this), 300);
}



void SomeClass::increase() {
    auto func = std::bind(&SomeClass::increaseImpl, this);

    func(); // expected-warning {{values with capability 'singleThreadExecutor' are leaked to unsafe call 'std::impl::binder<void (SomeClass::*)(), SomeClass *>::operator()'}} \
            // expected-note@-2 {{capability is traced from here}}
    singleThreadExecutor->exec(func);
}

void SomeClass::decrease() {
    auto decreaseImpl = [this]() { // expected-note {{lambda implicitly requires thread 'singleThreadExecutor'}}
        counter--; // expected-note {{... the thread was required for this statement}}
        onCounterChanged();
    };

    decreaseImpl(); // expected-warning {{values with capability 'singleThreadExecutor' are leaked to unsafe call 'SomeClass::decrease()::(anonymous class)::operator()'}}
    singleThreadExecutor->exec(decreaseImpl);
}

void SomeClass::touchCounter() {
    auto touch = [this]() {
        increaseImpl();
        counter--;

        this->counterChanged(); // expected-warning {{attempt to take the second thread capability 'callbackExecutor'}} \
                                // expected-note@-3 {{the first thread capability was 'singleThreadExecutor'}}
    };

    singleThreadExecutor->exec(touch);
}

void SomeClass::touchCounter2() {
  auto lambda1 = [this]() {
    this->callbackExecutor->assertInThread();
    counter += 1; // expected-warning {{attempt to take the second thread capability 'singleThreadExecutor'}} \
                  // expected-note@-1 {{the first thread capability was 'callbackExecutor'}}
  };
  (void) lambda1;

  auto lambda2 = [this]() { // expected-note {{lambda implicitly requires thread 'singleThreadExecutor'}}
    counter += 1; // expected-note {{... the thread was required for this statement}}
  };
  callbackExecutor->exec(lambda2); // expected-warning {{calling 'exec' acquires 'callbackExecutor' capability, but argument is associated with 'singleThreadExecutor' capability}}
}

int SomeClass::get() const {
    return counter; // expected-warning {{reading variable 'counter' requires holding thread 'singleThreadExecutor'}}
}

std::function<void()> SomeClass::getIncreaseImpl() {
    return [this]() { // expected-warning {{values with capability 'singleThreadExecutor' are leaked to unsafe ReturnStmt}} \
                      // expected-note {{lambda implicitly requires thread 'singleThreadExecutor'}}
        increaseImpl(); // expected-note {{... the thread was required for this statement}}
    };
}



void SomeClass::increaseImpl() {
    counter++;

    counter *= 10;
    divideCounterByTenImpl();

    onCounterChanged();
}

void SomeClass::divideCounterByTenImpl() {
    singleThreadExecutor->assertInThread();
    counter /= 10;
}



void SomeClass::onCounterChanged() {
    callbackExecutor->exec([this] {
        this->counterChanged();
    });
}



//=============================================================================

namespace overriden_method_requires_capability {

class A {
public:
  virtual ~A() = default;

  virtual void foo() = 0;
};

class B : public A {
public:
  void foo() override REQUIRES_THREAD(*executor) {} // expected-warning {{virtual function requires lock 'executor', but its base method does not}}

private:
  ThreadExecutor* executor;
};

class C {
public:
  virtual ~C() = default;

  virtual void foo() REQUIRES_THREAD(*executor1) {};

protected:
  ThreadExecutor* executor1;
};

class D : public C {
public:
  void foo() override REQUIRES_THREAD(*executor2) {} // todo: warning {{virtual function requires lock 'executor2', but its base method does not}}

private:
  ThreadExecutor* executor2;
};

}

//=============================================================================

namespace declared_multiple_thread_capabilities {

class A {
public:
  ThreadExecutor* executor1;
  ThreadExecutor* executor2;

  int bar() __attribute__((requires_capability(*executor1, *executor2))) { return 1; }; // \
      // expected-warning {{function cannot require several thread capabilities}}
  int value __attribute__((guarded_by(*executor1))) __attribute__((guarded_by(*executor2))) = 1; // \
      // expected-warning {{value cannot be guarded by several thread capabilities}}

  void foo() {
    auto valuePtr = &A::bar;
    (void) valuePtr;

    auto* valuePtr2 = &value;
    (void) valuePtr2;
  }
};

}

//=============================================================================

namespace local_var_following_many_threads {

struct Pair {
  Pair(int* a, int* b) {}
};

class A {
public:
  ThreadExecutor* executor1;
  ThreadExecutor* executor2;

  int value __attribute__((guarded_by(*executor1))) = 1;
  int value2 __attribute__((guarded_by(*executor2))) = 5;

  void foo() {
    auto* valuePtr = &value;
    *valuePtr = 4; // expected-warning {{values with capability 'executor1' are leaked to unsafe UnaryOperator}}

    // todo: maybe warning about different capabilities?
    Pair pair(&value, &value2); // expected-warning {{values with capability 'executor1' are leaked to unsafe object constructor 'local_var_following_many_threads::Pair'}} \
        // expected-note@-4 {{capability is traced from here}}
  }
};

}
