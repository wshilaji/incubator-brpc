/*
 *int asyncTask(int x) {
    std::this_thread::sleep_for(std::chrono::seconds(2)); // 模拟长时间运行的任务
    return x * x; // 返回平方
main : 
    // 启动异步任务
    std::future<int> result = std::async(std::launch::async, asyncTask, 5);
    std::cout << "Doing other work while waiting for the result...\n";
    // 等待结果并获取
    int value = result.get(); // 这会阻塞直到任务完成

    future都是需要几个固定的异步线程调用方式，比如async这种，
    但是很多时候不喜欢用这种异步方式，那么自然也就没办法获取future，所以为了future的获取更加灵活方便，就需要有这个promise对象。
promise demo API : 就与std::async不用绑定了。 下面都是直接thread 
主线程通知子线程
func : print_int (std::future<int>& fut) {
  cout<<"child thread  print_int"<<endl;
  int x = fut.get();
  std::cout << "value: " << x << '\n';
main : 
  std::promise<int> prom;                      // create promise
  std::future<int> fut = prom.get_future();    // engagement with future
  std::thread th1 (print_int, std::ref(fut));  // send future to new thread
  std::this_thread::sleep_for(std::chrono::seconds(1));
  cout<<"main thread  prom.set_value (10)"<<endl;
  prom.set_value (10);
  th1.join();}
  执行结果 child thread  print_int 
           main thread  prom.set_value (10) 
           value: 10

// 子线程通知主线程
func:  asyncTask(std::promise<int> &&p) {
    std::this_thread::sleep_for(std::chrono::seconds(2)); // 模拟长时间运行的任务
    p.set_value(42); // 设置结果
main: 
    // 创建 promise 和对应的 future
    std::promise<int> promise;
    std::future<int> future = promise.get_future();
    // 启动异步任务
    std::thread t(asyncTask, std::move(promise));
    std::cout << "Doing other work while waiting for the result...\n";
    // 等待结果并获取
    int value = future.get(); // 这会阻塞直到任务完成
 * */


  /* std::函数声明
    template<bool B, class T = void>
    struct enable_if {};//  B 为 true，enable_if 将定义一个名为 type 的类型别名，反之则为空

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value, T>::type
    Add(T a, T b);
    template<typename T>
    typename std::enable_if<!std::is_integral<T>::value, T>::type
    Add(T a, T b);

    std::enable_if_t 是 std::enable_if 的简化写法，等价于 typename std::enable_if<B, T>::type
    template<typename T>
    std::enable_if_t<std::is_integral_v<T>, T> myFunction(T value) {
        return value * 2; // 仅在 T 是整数类型时可用
    }
    
    template <typename C> constexpr bool is_condition_task_v = std::is_invocable_r_v<int, C>;
    std::is_invocable_r:这是一个类型特征，用于检查给定的可调用对象（如函数、函数指针、lambda 等）是否可以被调用，且其返回类型符合指定类型。std::is_invocable_r_v简化
    void func() {}
    int func_with_return() { return 42;  }
    template <typename C>
    void check_invocable() {
        if constexpr (std::is_invocable_r_v<int, C>) { //<A, C>  A是是C是否返回的类型可以转换成A , C是一个func或函数指针是否可以被调用就是是一个函数
            std::cout << "C is invocable and returns int.\n";
        } else {
            std::cout << "C is not invocable or does not return int.\n";
        }
    }
    check_invocable<void(*)()>(); // 输出: C is not invocable or does not return int.
    check_invocable<decltype(func_with_return)>(); // 输出: C is invocable and returns int.
                                                   //

    模板：is_protobuf_message 是一个模板结构体，用于检测类型 T 是否是 google::protobuf::Message 的子类。
    std::is_base_of：这是一个类型特征，检查 T 是否是 google::protobuf::Message 的派生类。如果是，则返回 true，否则返回 false。
    std::integral_constant<bool, ...>：这个模板用于将结果封装为常量值，方便在编译时使用。
    template <typename T>
    struct is_protobuf_message
        : std::integral_constant<bool,
              std::is_base_of<google::protobuf::Message, T>::value> {};

    template <typename T>
    inline constexpr bool is_protobuf_message_v = is_protobuf_message<T>::value;


    std::false_type 是 std::integral_constant<bool, false> 的一个别名，表示一个具有 value 成员的类型，该成员总是 false。
    #include <type_traits>
        template <typename T>
        struct is_custom_type : std::false_type {}; // 默认情况下，返回 false
        // 特化
        template <>
        struct is_custom_type<int> : std::true_type {}; // int 是自定义类型
        // 使用示例
        static_assert(is_custom_type<int>::value, "int should be a custom type");
        static_assert(!is_custom_type<double>::value, "double should not be a custom type");
   * */