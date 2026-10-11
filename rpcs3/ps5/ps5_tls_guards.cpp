// The C++ thread-local initialisation guards that no translation unit defines.
//
// WHY THIS FILE EXISTS
//
// The title is built with -femulated-tls, which the SDK's prospero-clang passes
// unconditionally. Under emulated TLS a thread_local's storage is not the real
// PT_TLS block: clang rewrites every access into an __emutls_get_address() call
// and copies the variable's initialiser image into per-thread storage on first
// touch. That is what makes the pthread_* imports in the title's .rela.plt
// (pthread_key_create, pthread_getspecific, pthread_setspecific, pthread_once)
// load-bearing -- they are the emutls runtime.
//
// The side effect is a clang behaviour that only shows up at the link, not in
// any one object file: the TU that *defines* a thread_local with external
// linkage stops emitting the Itanium ABI's thread-init guard, because under
// emutls the initialiser image does that job instead -- but every other TU that
// merely *uses* the variable still emits a call to that guard, because from
// where it stands it sees only `extern thread_local T x;` in a header and
// cannot prove the definition needs no dynamic initialisation.
//
// So the guard is referenced (a .rela.plt slot and a .got entry each) and never
// defined, which leaves it WEAK UND in .dynsym:
//
//   w _ZTH16g_tls_log_prefix
//   w _ZTH17g_tls_log_control
//   w _ZTH20g_tls_serialize_name
//   w _ZTHN10cpu_thread17g_tls_this_threadE
//   w _ZTHN10id_manager4g_idE
//   w _ZTHN11thread_ctrl17g_tls_this_threadE
//   w _ZTHN2fs11g_tls_errorE
//   w _ZTHN2vm12g_tls_lockedE
//   w _ZTHN7lv2_obj11g_to_notifyE
//   w _ZTHN7lv2_obj25g_postpone_notify_barrierE
//
// A weak-undefined symbol in .dynsym is a request the loader is expected to
// answer from the process's own exports. These ten are RPCS3's internal C++
// bookkeeping, so nothing on the console exports them -- libSceLibcInternal,
// libkernel or otherwise. The lookup fails, the slot keeps the zero the file
// initialised it to, and the first thread to touch one of these variables
// executes `call 0`. On Prospero address 0 is inside the image and holds an
// int3, so the title dies instantly with SIGTRAP at rip=0x400001, from
// thread_base::initialize() on the thread main_ps5.cpp starts.
//
// Note the contrast with the other weak-undefined names in .dynsym -- fclose,
// fflush, malloc_usable_size, __gcov_dump, __gcov_flush. Those are ordinary
// libc entry points that the console's libc *does* export, so the loader binds
// them normally; that is why CMakeLists.txt shims only the calls libc has no
// export for (accept4 and friends) and leaves these alone.
//
// WHY AN EMPTY BODY IS CORRECT, NOT A PATCH OVER THE PROBLEM
//
// The guard's only job is to run a variable's dynamic initialiser once per
// thread. All ten variables here are constant-initialised and trivially
// destructible:
//
//   logs.cpp:41           thread_local std::string(*g_tls_log_prefix)() = &default_string;
//   logs.cpp:44           thread_local void(*g_tls_log_control)(const char*, u64) = [](const char*, u64){};
//   System.cpp:108        thread_local std::string_view g_tls_serialize_name;
//   IdManager.cpp:8       thread_local u32 g_id = 0;
//   vm.cpp:77             thread_local atomic_t<cpu_thread*>* g_tls_locked = nullptr;
//   File.cpp:312          thread_local error g_tls_error = error::ok;
//   CPUThread.cpp:503     thread_local DECLARE(cpu_thread::g_tls_this_thread) = nullptr;
//   Thread.cpp:2807       thread_local DECLARE(thread_ctrl::g_tls_this_thread) = nullptr;
//   lv2.cpp:1357          thread_local DECLARE(lv2_obj::g_to_notify){};              // std::add_pointer_t<const void>[4]
//   lv2.cpp:1358          thread_local DECLARE(lv2_obj::g_postpone_notify_barrier){}; // bool
//
// An initialiser that is a constant expression (an address, a null pointer, a
// literal, an aggregate zero-fill) is applied when the storage is created, so
// the guard the compiler would have emitted for each of these is an empty
// function anyway. Nothing is skipped by stubbing them: the emutls initialiser
// image still runs, which is what actually initialises the storage here.
//
// HOW THE NAMES ARE SPELLED
//
// Every mangled name above is already a valid C identifier, so declaring the
// function `extern "C"` gives the symbol that exact name with no asm label
// needed -- the C++ ABI prefix is the whole reason the names look like that in
// the first place. Defining them strongly also beats the weak undefined
// references cleanly at link time, so the PLT slots stop pointing at zero.

#define PS5_TLS_GUARD(name) extern "C" void name(void) {}

PS5_TLS_GUARD(_ZTH16g_tls_log_prefix)
PS5_TLS_GUARD(_ZTH17g_tls_log_control)
PS5_TLS_GUARD(_ZTH20g_tls_serialize_name)
PS5_TLS_GUARD(_ZTHN10cpu_thread17g_tls_this_threadE)
PS5_TLS_GUARD(_ZTHN10id_manager4g_idE)
PS5_TLS_GUARD(_ZTHN11thread_ctrl17g_tls_this_threadE)
PS5_TLS_GUARD(_ZTHN2fs11g_tls_errorE)
PS5_TLS_GUARD(_ZTHN2vm12g_tls_lockedE)
PS5_TLS_GUARD(_ZTHN7lv2_obj11g_to_notifyE)
PS5_TLS_GUARD(_ZTHN7lv2_obj25g_postpone_notify_barrierE)

#undef PS5_TLS_GUARD
