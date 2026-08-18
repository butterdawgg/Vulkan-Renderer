#include "app.h"

#include "debug_utils.h"

int main()
{
    try
    {
        App app { };

        app.Run();
    }
    catch (std::exception& e)
    {
        logCaughtException(e.what());
    }
}