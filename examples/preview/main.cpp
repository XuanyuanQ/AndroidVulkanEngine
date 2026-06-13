#include "PreviewApp.h"

#include <exception>
#include <iostream>

#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#include <cstdlib>
#endif

int main(int argc, char** argv)
{
#if defined(_MSC_VER) && !defined(NDEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    try {
        return RunPreview(argc, argv);
    } catch (std::exception const& exc) {
        std::cerr << "[preview] " << exc.what() << "\n";
        return 2;
    }
}
