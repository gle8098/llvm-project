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
#define TRY_ASSERT(...)         __attribute__((try_assert_capability(__VA_ARGS__)))
#define TRY_ASSERT_SHARED(...)  __attribute__((try_assert_shared_capability(__VA_ARGS__)))

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
DETACHED_CAPABILITY_HOLDER("std::move")

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

// simple std::move without remove_reference<T>
template<typename T>
[[nodiscard]] constexpr T&& move(T&& __t) noexcept {
  return static_cast<T&&>(__t);
}

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

    const T *operator*() const { return ptr; }
    T *operator*() { return ptr; }
};

}

//=============================================================================

template<typename K, typename V>
struct pair {
  K First;
  V Second;
  pair(const K &f, const V &s) : First(f), Second(s) {}

  K getFirst() {
    return First;
  }

  V getSecond() {
    return Second;
  }
};

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

    virtual bool isInThread() const noexcept TRY_ASSERT(true) = 0;

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
                                                   // expected-note {{lambda implicitly requires thread 'singleThreadExecutor' for a statement here}}
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
                    // expected-note@-4 {{lambda implicitly requires thread 'singleThreadExecutor' for a statement here}}
    singleThreadExecutor->exec(decreaseImpl);
}

extern bool *flag1, *flag2;

void SomeClass::touchCounter() {
    auto touch = [this]() {
        increaseImpl();
        counter--;

        this->counterChanged(); // expected-warning {{reading variable 'counterChanged' requires holding thread 'callbackExecutor'}}
    };

    singleThreadExecutor->exec(touch);

    auto touchOnFlags = [this]() {
        if (*flag1) {
            increaseImpl();
        }
        if (*flag2) {
            increaseImpl();
        }
    };
    singleThreadExecutor->exec(touchOnFlags);
}

void SomeClass::touchCounter2() {
  auto lambda1 = [this]() {
    this->callbackExecutor->assertInThread(); // asserted, not added as dynamic req
    counter += 1;
  };
  lambda1(); // expected-warning {{calling function 'operator()' requires holding thread 'singleThreadExecutor' exclusively}} \
             // expected-note@-2 {{lambda implicitly requires thread 'singleThreadExecutor' for a statement here}}

  auto lambda2 = [this]() {
    counter += 1;
  };
  callbackExecutor->exec(lambda2); // expected-warning {{argument of 'exec' requires 'singleThreadExecutor' capability}} \
                                   // expected-note {{acquired capabilities: 'callbackExecutor'}} \
                                   // expected-note@-2 {{lambda implicitly requires thread 'singleThreadExecutor' for a statement here}}

  callbackExecutor->exec(std::move(lambda2)); // expected-warning {{argument of 'exec' requires 'singleThreadExecutor' capability}} \
                                   // expected-note {{acquired capabilities: 'callbackExecutor'}} \
                                   // expected-note@-6 {{lambda implicitly requires thread 'singleThreadExecutor' for a statement here}}
}

void SomeClass::callingGivenLambda(std::function<void()> lambda) {
  lambda();
}

void SomeClass::useCallingGivenLambda() {
  singleThreadExecutor->exec([this] {
    callingGivenLambda([this] { counter++; }); // expected-warning {{requirement on thread 'singleThreadExecutor' is discarded when function object is passed as argument to 'callingGivenLambda'}} \
                                               // expected-note {{lambda implicitly requires thread 'singleThreadExecutor' for a statement here}}
  });
}

int SomeClass::get() const {
    return counter; // expected-warning {{reading variable 'counter' requires holding thread 'singleThreadExecutor'}}
}

std::function<void()> SomeClass::getIncreaseImpl() {
  return [this]() { // expected-warning {{requirement on thread 'singleThreadExecutor' is discarded when function object is being returned}} \
                    // expected-note@+2 {{lambda implicitly requires thread 'singleThreadExecutor' for a statement here}}
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

  virtual void bar() REQUIRES(m) = 0;

  virtual void hard(Mutex* given) = 0;

protected:
  Mutex m;
};

class B : public A {
public:
  void foo() override REQUIRES(*executor) {} // expected-warning {{virtual function requires lock 'executor', but its base method does not}}
  void bar() override REQUIRES(executor) {} // expected-warning {{virtual function requires lock 'executor', but its base method does not}}
  void hard(Mutex* given) override REQUIRES(given) {}; // expected-warning {{virtual function requires lock 'given', but its base method does not}}

private:
  ThreadExecutor* executor;
};

class B1 : public A {
public:
  void foo() override EXCLUDES(executor) {} // that's okay

  void bar() override REQUIRES(m) REQUIRES(!executor) {} // that's okay

private:
  ThreadExecutor* executor;
};

class B2 : public A {
public:
  void foo() override {} // that's okay

  void bar() override {} // that's okay
};

class C {
public:
  virtual ~C() = default;

  virtual void foo() REQUIRES(*executor1, m) {};

protected:
  ThreadExecutor* executor1;
  Mutex m;
};

class D : public C {
public:
  void foo() override REQUIRES(*executor2) {} // expected-warning {{virtual function requires lock 'executor2', but its base method does not}}

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

namespace capabilities_for_function_pointer {

void (*getValuePtrGlobal)() = nullptr;

class A {
public:
  ThreadExecutor* executor;

  std::function<void()> getValuePtr;
  void (*getValuePtr2)() = nullptr;

  void foo() {
    std::function<void()> getValueLTmp = []() REQUIRES(executor) {};
    getValuePtr = getValueLTmp; // expected-warning {{requirement on thread 'executor' is discarded when function object is passed as argument to 'operator='}} \
                                // expected-note@-1 {{capability 'executor' is traced from here}}
    getValuePtr = std::function<void()>([]() REQUIRES(executor) {}); // expected-warning {{requirement on thread 'executor' is discarded when function object is passed as argument to 'operator='}}
  }

  void bar() {
    void (*getValuePtrTmp)() = nullptr;
    getValuePtrTmp = []() REQUIRES(executor) {};
    getValuePtr2 = []() REQUIRES(executor) {}; // expected-warning {{requirement on thread 'executor' is discarded when function object is assigned to a field}} \
                                               // expected-note@-1 {{capability 'executor' is traced from here}}
    getValuePtr2 = getValuePtrTmp; // expected-warning {{requirement on thread 'executor' is discarded when function object is assigned to a field}} \
                                   // expected-note@-2 {{capability 'executor' is traced from here}}
    getValuePtrGlobal = getValuePtrTmp; // expected-warning {{requirement on thread 'executor' is discarded when function object is assigned to a field}} \
                                        // expected-note@-5 {{capability 'executor' is traced from here}}
  }

  void bar2() {
    auto value = new std::function<void()>([]() REQUIRES(executor) {});
    (*value)(); // expected-warning {{calling function 'operator()' requires holding thread 'executor' exclusively}} \
                // expected-note@-1 {{capability 'executor' is traced from here}}
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
  static_cast<void (*)()>(&foo)(); // expected-warning {{calling function 'foo' requires holding mutex 'm' exclusively}} \
                                   // expected-note {{capability 'm' is traced from here}}
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
    fooPtr = &foo;
  } else {
    fooPtr = &fooLocked; // expected-warning {{statement DeclRefExpr does not fit into tracking values model (second tracking capability)}}
  }
  fooPtr();
}
void bar_loop() {
  void (*fooPtr)() = nullptr;
  while (condition()) {
    if (condition()) {
      fooPtr = &foo;
    } else {
      fooPtr = &fooLocked; // expected-warning {{statement DeclRefExpr does not fit into tracking values model (second tracking capability)}}
    }
  }
  fooPtr();
}

}

//=============================================================================

namespace negative_caps {

class Testing {

void special() REQUIRES(!executor_.get());

void special_not() REQUIRES(executor_);

void foo() {
  executor_.get()->exec([this] { // expected-warning {{argument of 'exec' requires '!executor_' capability}} \
                                 // expected-note {{acquired capabilities: 'executor_'}}
    special(); // expected-note {{lambda implicitly requires thread '!executor_' for a statement here}}
  });

  special(); // expected-warning {{calling function 'special' requires negative capability '!executor_'}}
  executor_->assertInThread();
  special(); // expected-warning {{cannot call function 'special' while thread 'executor_' is held}}
}

void foo2() REQUIRES(!executor_.get()) {
  special();
}

void foo3() REQUIRES(executor_.get()) {
  special(); // expected-warning {{cannot call function 'special' while thread 'executor_' is held}}
}

unique_ptr<ThreadExecutor> executor_;

};

}

//=============================================================================

namespace declared_contradictory_locks {

class Testing {

  void foo1() REQUIRES(!executor_.get()) REQUIRES(executor_.get()) {
    // expected-warning@-1 {{function 'foo1' requires and excludes thread 'executor_' at once}}
  }
  
  void foo2() REQUIRES(!executor_.get()) REQUIRES(executor_) {
    // expected-warning@-1 {{function 'foo2' requires and excludes thread 'executor_' at once}}
  }
  
  void foo3() EXCLUDES(executor_.get()) REQUIRES(executor_.get()) {
    // expected-warning@-1 {{function 'foo3' requires and excludes thread 'executor_' at once}}
  }
  
  void foo4() REQUIRES(executor_) REQUIRES(!executor_.get())  {
    // expected-warning@-1 {{function 'foo4' requires and excludes thread 'executor_' at once}}
  }
  
  void foo5() REQUIRES(executor_) EXCLUDES(executor_.get())  {
    // expected-warning@-1 {{function 'foo5' requires and excludes thread 'executor_' at once}}
  }

  unique_ptr<ThreadExecutor> executor_;

};

}


//=============================================================================

namespace smart_pointer_as_capability {

class CAPABILITY("some_cap") my_unique_ptr : public unique_ptr<int> {};

class Testing {

void foo() {
  some_value++; // expected-warning {{writing variable 'some_value' requires holding thread 'executor_' exclusively}}
  some_value_2++; // expected-warning {{writing variable 'some_value_2' requires holding some_cap 'some_lock' exclusively}}
}

unique_ptr<ThreadExecutor> executor_;
my_unique_ptr some_lock;
int some_value GUARDED_BY(executor_);
int some_value_2 GUARDED_BY(some_lock);

};

}

namespace negative_capabilities_on_private_fields {

class MyLogic {
  Mutex m;
  ThreadExecutor* executor;
  unique_ptr<ThreadExecutor> executorUPtr;

  int some = 0;

public:
  void callA() REQUIRES(!m) {
    some++;
  }

  void callB() REQUIRES(!executor) {
    some++;
  }

  void callC() REQUIRES(!executorUPtr.get()) {
    some++;
  }

  void actuallyWorks() {
    callA(); // expected-warning {{calling function 'callA' requires negative capability '!m'}}
    callB(); // expected-warning {{calling function 'callB' requires negative capability '!executor'}}
    callC(); // expected-warning {{calling function 'callC' requires negative capability '!executorUPtr'}}
  }
};

void just_invoke(std::function<void()> func) {
  func();
}

void outside_fun(MyLogic& logic) {
  logic.callA();
  logic.callB();
  logic.callC();

  ThreadExecutor* executor;
  executor->exec([&] { logic.callA(); });
  executor->exec([&] { logic.callB(); });
  executor->exec([&] { logic.callC(); });

  just_invoke([&] { logic.callA(); });
  just_invoke([&] { logic.callB(); });
  just_invoke([&] { logic.callC(); });
}

}

namespace no_tracking_capability {

ThreadExecutor *executor;
int counter GUARDED_BY(executor);

void foo() {
  auto callback NO_TRACKING_CAPABILITY = std::function<int()>([]() {
   return counter;
  });
  callback(); // errornous, but no diagnostic

  int a NO_TRACKING_CAPABILITY = 0;
  executor->exec([&a]{ a++; }); // errornous, but no unsafe reference check
}

}

namespace try_assert {

ThreadExecutor *executor;

void bar() REQUIRES(executor);

void foo() {
  bool good_thread = executor->isInThread();
  if (good_thread) {
    bar();
  } else {
    bar(); // expected-warning {{calling function 'bar' requires holding thread 'executor' exclusively}}
  }
}

}

namespace do_not_ignore_public_requires {

class A {
private:
  ThreadExecutor* executor;

public:
  void foo() REQUIRES(executor) {}
};

void foo() {
  A* a;
  a->foo(); // expected-warning {{calling function 'foo' requires holding thread 'a->executor' exclusively}}
}

}

namespace callback_with_requires {

class A {

Mutex m;
Mutex fakeM;
std::function<void()> toBeCalledWithM REQUIRES(this->m);

void assign() {
  auto lambda = []() REQUIRES(this->m) {};
  toBeCalledWithM = lambda;

  auto badLambda = []() REQUIRES(this->fakeM) {};
  toBeCalledWithM = badLambda; // expected-warning {{requirement on mutex 'fakeM' is discarded when function object is passed as argument to 'operator='}} \
                               // expected-note@-1 {{capability 'fakeM' is traced from here}}

  auto lambdaNoCap = []() {};
  toBeCalledWithM = lambdaNoCap;
}

std::function<void()> requires_origin() {
  toBeCalledWithM(); // expected-warning {{calling function 'operator()' requires holding mutex 'm' exclusively}}

  m.Lock();
  toBeCalledWithM();
  m.Unlock();

  auto l = std::move(toBeCalledWithM); // we may move out the object, or access it by value anyhow

  // but we cannot invoke it or return (moving out of the scope => RequiresCapabilityAttr would be missed)
  l(); // expected-warning {{calling function 'operator()' requires holding mutex 'm' exclusively}} \
       // expected-note@-3 {{capability 'm' is traced from here}}
  return l; // expected-warning {{requirement on mutex 'm' is discarded when function object is being returned}} \
            // expected-note@-5 {{capability 'm' is traced from here}}
}

};

}

namespace mix_of_dynrequires_and_explicit_lock {

ThreadExecutor* executor;
Mutex m;

int data_exec GUARDED_BY(executor);
int data_m GUARDED_BY(m);

void foo() {
  executor->exec([] {
    MutexLock lock(&m);
    data_m = 5;
    data_exec = 6;
  });


  executor->exec([] {
    data_exec = 6;

    MutexLock lock(&m);
    data_m = 5;
    data_exec = 6;
  });
}

}

namespace dynamic_attr_with_scopes {

extern bool *flag1, *flag2;

ThreadExecutor *executor;

void bar() REQUIRES(executor) {}
void not_bar() REQUIRES(!executor) {}
void not_bar_soft() EXCLUDES(executor) {}

void foo() {  
  executor->exec([]() { // expected-warning {{argument of 'exec' requires '!executor' capability}} \
                        // expected-note {{acquired capabilities: 'executor'}}
    not_bar_soft(); // mentions, but does not requires !executor
    if (*flag1) {
      // cannot add dynamic requires for executor -- already mentioned
      bar(); // expected-warning {{calling function 'bar' requires holding thread 'executor' exclusively}}
    }
    if (*flag2) {
      // cannot add dynamic requires for executor -- already mentioned
      bar(); // expected-warning {{calling function 'bar' requires holding thread 'executor' exclusively}}
    }
    while (*flag1 && *flag2) {
      // can add dynamic requires for executor -- negative mentioned and negative required here
      // imho somewhat strange behavior, but still correct
      not_bar(); // expected-note {{lambda implicitly requires thread '!executor' for a statement here}}
    }
  });
}

void foo3() {  
  executor->exec([]() {
    if (*flag1) {
      bar(); // dynamically adds executor
    }
    if (*flag2) {
      bar();
    }
    while (*flag1 && *flag2) {
      not_bar(); // expected-warning {{cannot call function 'not_bar' while thread 'executor' is held}}
    }
  });
}

void foo_negative() {
  auto lambda1 = []() {
    not_bar(); // requires dyn attr, ok
    not_bar_soft();
  };

  auto lambda2 = []() {
    not_bar_soft();
    not_bar(); // requires negative dyn attr, ok because it is safe with EXCLUDES(same_cap)
  };
}

Mutex m;

int m_data GUARDED_BY(m);
void bar_m() REQUIRES(m) {}

void foo2() {
  auto lambda = []() /*REQUIRES(m)*/ {
    {
      m.Lock();
      bar_m();
      m.Unlock();
    }
  
    m_data++; // expected-warning {{writing variable 'm_data' requires holding mutex 'm' exclusively}}
  };
}

void foo2_before() {
  auto lambda = []() /*REQUIRES(m)*/ {
    int i = m_data; // Ok, take m as dynamic requires attr

    {
      m.Lock(); // expected-warning {{acquiring mutex 'm' that is already held}} \
                // expected-note@-3 {{lambda implicitly requires mutex 'm' for a statement here}}
      bar_m();
      m.Unlock();
    }
  
    m_data++; // expected-warning {{writing variable 'm_data' requires holding mutex 'm' exclusively}}
  }; // expected-warning {{expecting mutex 'm' to be held at the end of function}} \
     // expected-note@-10 {{lambda implicitly requires mutex 'm' for a statement here}}
}

void foo2_no_scope() {
  auto lambda = []() /*REQUIRES(m)*/ {
    int i = m_data; // Ok, take m as dynamic requires attr

    m.Lock(); // expected-warning {{acquiring mutex 'm' that is already held}} \
                // expected-note@-2 {{lambda implicitly requires mutex 'm' for a statement here}}
    bar_m();
    m.Unlock();
  
    m_data++; // expected-warning {{writing variable 'm_data' requires holding mutex 'm' exclusively}}
  }; // expected-warning {{expecting mutex 'm' to be held at the end of function}} \
     // expected-note@-8 {{lambda implicitly requires mutex 'm' for a statement here}}
}

void foo2_no_scope_excludes() {
  auto lambda = []() EXCLUDES(m) {
    // cannot add m as dynamic requires, m is mentioned in declaration (EXCLUDES)
    int i = m_data; // expected-warning {{reading variable 'm_data' requires holding mutex 'm'}}

    m.Lock();
    bar_m();
    m.Unlock();
  
    m_data++; // expected-warning {{writing variable 'm_data' requires holding mutex 'm' exclusively}}
  };
}

}



