// RUN: %clang_cc1 -fsyntax-only -verify -std=c++17 -Wthread-safety -Wthread-safety-ste -Wthread-safety-ste-model-failure -fcxx-exceptions %s

//=============================================================================

#if !__has_feature(c_thread_safety_ste)
#error "Feature disabled"
#endif

#define CAPABILITY(x) __attribute__((capability(x)))
#define LOCKABLE CAPABILITY("mutex")
#define THREAD_CAPABILITY CAPABILITY("thread")
#define SCOPED_LOCKABLE __attribute__((scoped_lockable))

#define ACQUIRE(...)            __attribute__((acquire_capability(__VA_ARGS__)))
#define ACQUIRE_SHARED(...)     __attribute__((acquire_shared_capability(__VA_ARGS__)))
#define RELEASE_GENERIC(...)    __attribute__((release_generic_capability(__VA_ARGS__)))
#define RELEASE(...)            __attribute__((release_capability(__VA_ARGS__)))
#define RELEASE_SHARED(...)     __attribute__((release_shared_capability(__VA_ARGS__)))
#define TRY_ACQUIRE(...)        __attribute__((try_acquire_capability(__VA_ARGS__)))
#define TRY_ACQUIRE_SHARED(...) __attribute__((try_acquire_shared_capability(__VA_ARGS__)))

#define GUARDED_BY(x) __attribute__((guarded_by(x)))
#define PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))

#define REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define REQUIRES_SHARED(...) __attribute__((requires_shared_capability(__VA_ARGS__)))
#define EXCLUDES(...) __attribute__((locks_excluded(__VA_ARGS__)))

#define ASSERT_CAPABILITY(x) __attribute__((assert_capability(x)))
#define ASSERT_SHARED_CAPABILITY(x) __attribute__((assert_shared_capability(x)))

#define EXECUTE_WITH_CAPABILITY(...) __attribute__((execute_with_capability(__VA_ARGS__)))
#define DETACHED_EXECUTE_WITH_CAPABILITY(...) [[clang::det_execute_with_capability(__VA_ARGS__)]];

#define CAPABILITY_HOLDER(x) __attribute__((capability_holder(x)))
#define DETACHED_CAPABILITY_HOLDER(x) [[clang::det_capability_holder(x)]];

#define NO_TRACKING_CAPABILITY __attribute__((no_tracking_capability))
#define NO_THREAD_SAFETY_ANALYSIS __attribute__((no_thread_safety_analysis))

DETACHED_CAPABILITY_HOLDER("std::function")
DETACHED_CAPABILITY_HOLDER("std::bind")

//=============================================================================

class LOCKABLE Mutex {
 public:
  void Lock() ACQUIRE();
  void ReaderLock() ACQUIRE_SHARED();
  void Unlock() RELEASE_GENERIC();
  void ExclusiveUnlock() RELEASE();
  void ReaderUnlock() RELEASE_SHARED();
  bool TryLock() TRY_ACQUIRE(true);
  bool ReaderTryLock() TRY_ACQUIRE_SHARED(true);
  void LockWhen(const int &cond) ACQUIRE();

  void PromoteShared()   RELEASE_SHARED() ACQUIRE();
  void DemoteExclusive() RELEASE() ACQUIRE_SHARED();

  // for negative capabilities
  const Mutex& operator!() const { return *this; }

  void AssertHeld()       ASSERT_CAPABILITY();
  void AssertReaderHeld() ASSERT_SHARED_CAPABILITY();
};

class SCOPED_LOCKABLE MutexLock {
 public:
  MutexLock(Mutex *mu) ACQUIRE(mu);
  MutexLock(Mutex *mu, bool adopt) ACQUIRE(mu);
  ~MutexLock() RELEASE_GENERIC();
};

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
    function(function const& rhs) = default;

    R operator()(Args... args)
    {
        return this->invoke_f();
    }
};

}

//=============================================================================

namespace {

template <typename T>
class unique_ptr {
public:
    typedef T* pointer;

    T* ptr = nullptr;

    pointer get() { return ptr; }
    T* get_no_typedef() { return ptr; }

    const T *operator->() const { return ptr; }
    T *operator->() { return ptr; }
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

    virtual void assertInThread() const noexcept ASSERT_CAPABILITY() = 0;

    inline bool exec(TaskBody taskBody, TaskCallerContext taskCallerContext = {}) EXECUTE_WITH_CAPABILITY()
    {
        return execImpl(taskBody, taskCallerContext);
    };

    virtual PeriodicTask execPeriodicTask(TaskBody taskBody, int period) = 0;

protected:
    /**
     * see bool exec(TaskBody taskBody, std::optional<TaskCallerContext> taskCallerContext = {})
     */
    virtual bool execImpl(TaskBody taskBody, TaskCallerContext taskCallerContext) = 0;
};
DETACHED_EXECUTE_WITH_CAPABILITY("ThreadExecutor::execPeriodicTask")

//=============================================================================

class SomeClass {
public:
    SomeClass();

    void increase();
    void decrease();
    void touchCounter(); ///< calls increaseImpl, then decreases counter & invokes callback
    void touchCounter2();

    void callingGivenLambda(std::function<void()>);
    void useCallingGivenLambda();

    int get() const;
    std::function<void()> getIncreaseImpl();

private:
    void init();
    void startImpl() REQUIRES(*singleThreadExecutor);

    void increaseImpl() REQUIRES(*singleThreadExecutor);
    void divideCounterByTenImpl();

    void onCounterChanged();

    ThreadExecutor* singleThreadExecutor = nullptr;
    ThreadExecutor* callbackExecutor;

    PeriodicTask periodicCounterIncrease GUARDED_BY(*singleThreadExecutor);
    int counter GUARDED_BY(*singleThreadExecutor) = 0;

    std::function<void()> counterChanged GUARDED_BY(*callbackExecutor);
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
  callbackExecutor->exec([this] { startImpl(); }); // expected-warning {{argument of 'exec' requires 'singleThreadExecutor' capability}} \
                                                   // expected-note {{acquired capabilities: 'callbackExecutor'}} \
                                                   // expected-note {{lambda implicitly requires thread 'singleThreadExecutor' for this statement}}
}

void SomeClass::startImpl() {
  counter = 1;
  periodicCounterIncrease = singleThreadExecutor->execPeriodicTask(std::bind(&SomeClass::increaseImpl, this), 300);
  (void) callbackExecutor->execPeriodicTask(std::bind(&SomeClass::increaseImpl, this), 300); // expected-warning {{argument of 'execPeriodicTask' requires 'singleThreadExecutor' capability}} \
                                                                                             // expected-note {{acquired capabilities: 'callbackExecutor'}}
}



void SomeClass::increase() {
    auto func = std::bind(&SomeClass::increaseImpl, this);

    func(); // expected-warning {{calling function 'operator()<>' requires holding thread 'singleThreadExecutor' exclusively}} \
            // expected-note@-2 {{capability 'singleThreadExecutor' is traced from here}}
    (func)();  // expected-warning {{calling function 'operator()<>' requires holding thread 'singleThreadExecutor' exclusively}} \
               // expected-note@-4 {{capability 'singleThreadExecutor' is traced from here}}
    (std::function<void()>(func))(); // expected-warning {{calling function 'operator()' requires holding thread 'singleThreadExecutor' exclusively}} \
                                     // expected-note@-6 {{capability 'singleThreadExecutor' is traced from here}}

    singleThreadExecutor->exec(func);
    callbackExecutor->exec(func); // expected-warning {{argument of 'exec' requires 'singleThreadExecutor' capability}} \
                                  // expected-note {{acquired capabilities: 'callbackExecutor'}}
}

void SomeClass::decrease() {
    auto decreaseImpl = [this]() {
        counter--;
        onCounterChanged();
    };

    decreaseImpl(); // expected-warning {{calling function 'operator()' requires holding thread 'singleThreadExecutor' exclusively}} \
                    // expected-note@-4 {{lambda implicitly requires thread 'singleThreadExecutor' for this statement}}
    singleThreadExecutor->exec(decreaseImpl);
}

void SomeClass::touchCounter() {
    auto touch = [this]() {
        increaseImpl();
        counter--;

        this->counterChanged(); // expected-warning {{reading variable 'counterChanged' requires holding thread 'callbackExecutor'}}
    };

    singleThreadExecutor->exec(touch);
}

void SomeClass::touchCounter2() {
  auto lambda1 = [this]() {
    this->callbackExecutor->assertInThread();
    counter += 1; // expected-warning {{writing variable 'counter' requires holding thread 'singleThreadExecutor' exclusively}}
  };
  (void) lambda1;

  auto lambda2 = [this]() {
    counter += 1;
  };
  callbackExecutor->exec(lambda2); // expected-warning {{argument of 'exec' requires 'singleThreadExecutor' capability}} \
                                   // expected-note {{acquired capabilities: 'callbackExecutor'}} \
                                   // expected-note@-2 {{lambda implicitly requires thread 'singleThreadExecutor' for this statement}}
}

void SomeClass::callingGivenLambda(std::function<void()> lambda) {
  lambda();
}

void SomeClass::useCallingGivenLambda() {
  singleThreadExecutor->exec([this] {
    callingGivenLambda([this] { counter++; }); // expected-warning {{functional object requiring thread 'singleThreadExecutor' loses its annotation by passing as argument to function call}} \
                                               // expected-note {{lambda implicitly requires thread 'singleThreadExecutor' for this statement}}
  });
}

int SomeClass::get() const {
    return counter; // expected-warning {{reading variable 'counter' requires holding thread 'singleThreadExecutor'}}
}

std::function<void()> SomeClass::getIncreaseImpl() {
  return [this]() { // expected-warning {{functional object requiring thread 'singleThreadExecutor' loses its annotation by returning from function}} \
                    // expected-note@+2 {{lambda implicitly requires thread 'singleThreadExecutor' for this statement}}
    increaseImpl();
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
  void foo() override REQUIRES(*executor) {} // expected-warning {{virtual function requires lock 'executor', but its base method does not}}

private:
  ThreadExecutor* executor;
};

class C {
public:
  virtual ~C() = default;

  virtual void foo() REQUIRES(*executor1) {};

protected:
  ThreadExecutor* executor1;
};

class D : public C {
public:
  void foo() override REQUIRES(*executor2) {} // todo: warning {{virtual function requires lock 'executor2', but its base method does not}}

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

  int bar() REQUIRES(*executor1, *executor2) { return 1; };
  int value GUARDED_BY(*executor1) GUARDED_BY(*executor2) = 1;

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
    *valuePtr = 4; // todo: expected warning {{values with capability 'executor1' are leaked to unsafe UnaryOperator}}

    // todo: maybe warning about different capabilities?
    Pair pair(&value, &value2); // todo: expected warning {{values with capability 'executor1' are leaked to unsafe object constructor 'local_var_following_many_threads::Pair'}} \
        // todo: expected note@-4 {{capability is traced from here}}
  }
};

}

//=============================================================================

namespace model_failure {

class A {
public:
  ThreadExecutor* executor;

  std::function<void()> getValuePtr;
  void (*getValuePtr2)() = nullptr;

  void foo() {
    std::function<void()> getValueLTmp = []() REQUIRES(executor) {};
    getValuePtr = getValueLTmp; // expected-warning {{functional object requiring thread 'executor' loses its annotation by passing as argument to function call}} \
                                // expected-note@-1 {{capability 'executor' is traced from here}}
    getValuePtr = std::function<void()>([]() REQUIRES(executor) {}); // expected-warning {{functional object requiring thread 'executor' loses its annotation by passing as argument to function call}}

    void (*getValuePtrTmp)() = nullptr;
    getValuePtrTmp = []() REQUIRES(executor) {};
    getValuePtr2 = []() REQUIRES(executor) {}; // expected-warning {{functional object requiring thread 'executor' loses its annotation by assigning to field}} \
                                               // expected-note@-1 {{capability 'executor' is traced from here}}
    getValuePtr2 = getValuePtrTmp; // expected-warning {{functional object requiring thread 'executor' loses its annotation by assigning to field}} \
                                   // expected-note@-2 {{capability 'executor' is traced from here}}

    new std::function<void()>([]() REQUIRES(executor) {});
  }
};

}

//=============================================================================

namespace execute_with_capability {

class A {
public:
  Mutex mA, mB, mC;
  int valueA GUARDED_BY(mA);
  int valueB GUARDED_BY(mB);
  int valueC GUARDED_BY(mC);

  void forEachNumber(std::function<void(int)> pred) EXECUTE_WITH_CAPABILITY("*") {
    for (int i = 0; i < 5; ++i) {
      pred(i);
    }
  }

  void forEachNumber2(std::function<void(int)> pred) EXECUTE_WITH_CAPABILITY(mB, mC) {
    for (int i = 0; i < 5; ++i) {
      pred(i);
    }
  }

  void forEachNumber3(std::function<void(int)> pred) EXECUTE_WITH_CAPABILITY(nullptr) {
    for (int i = 0; i < 5; ++i) {
      pred(i);
    }
  }

  void testForEachNumber() {
    auto lambda = [this](int a) REQUIRES(mA, mB) { valueA += a; valueB += a; };

    forEachNumber(lambda); // expected-warning {{argument of 'forEachNumber' requires 'mA' capability}}

    mA.Lock();
    forEachNumber(lambda); // expected-warning {{argument of 'forEachNumber' requires 'mB' capability}} \
                           // expected-note {{acquired capabilities: 'mA'}}

    mA.Unlock();
    mB.Lock();
    forEachNumber(lambda); // expected-warning {{argument of 'forEachNumber' requires 'mA' capability}} \
                           // expected-note {{acquired capabilities: 'mB'}}

    mA.Lock();
    forEachNumber(lambda); // ok

    mB.Unlock();
    mA.Unlock();
  }

  void testForEachNumberWithFixedMutexes() {
    forEachNumber2([this](int a) REQUIRES(mA, mC) { valueA++; valueC++; }); // expected-warning {{argument of 'forEachNumber2' requires 'mA' capability}} \
                                                                            // expected-note {{acquired capabilities: 'mB', 'mC'}}

    forEachNumber2([this](int a) REQUIRES(mC, mB) { valueC++; valueB++; }); // ok

    forEachNumber3([this](int a) REQUIRES(mC, mB) { valueC++; valueB++; }); // expected-warning {{argument of 'forEachNumber3' requires 'mC' capability}}
  }
};

}

//=============================================================================

namespace explicit_cast {

Mutex m;
void foo() REQUIRES(m) {}
void bar() {
  static_cast<void (*)()>(&foo)(); // expected-warning {{}}
}

}

//=============================================================================

namespace conditional_capability {

Mutex m;
bool condition();
void foo() REQUIRES(!m) {}
void fooLocked() REQUIRES(m) {}
void bar() {
  void (*fooPtr)() = nullptr;
  if (condition()) {
    fooPtr = &foo; // expected-warning {{statement DeclRefExpr does not fit into tracking values model (second tracking capability)}}
  } else {
    fooPtr = &fooLocked;
  }
  fooPtr();
}
void bar_loop() {
  void (*fooPtr)() = nullptr;
  while (condition()) {
    if (condition()) {
      fooPtr = &foo; // expected-warning {{statement DeclRefExpr does not fit into tracking values model (second tracking capability)}}
    } else {
      fooPtr = &fooLocked;
    }
  }
  fooPtr();
}

}

//=============================================================================

namespace negative_caps {

class Testing {

void special() REQUIRES(!executor_.get());

void foo() {
    executor_.get()->exec([this] { // expected-warning {{argument of 'exec' requires '!executor_' capability}} \
                                   // expected-note {{acquired capabilities: 'executor_'}}
        special(); // expected-note {{lambda implicitly requires thread '!executor_' for this statement}}
    });

    special(); // expected-warning {{calling function 'special' requires negative capability '!executor_'}}
    executor_->assertInThread();
    special(); // expected-warning {{cannot call function 'special' while thread 'executor_' is held}}
}

void foo2() REQUIRES(!executor_.get()) {
    special();
}

unique_ptr<ThreadExecutor> executor_;

};

}
