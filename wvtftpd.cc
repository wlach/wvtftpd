#include "wvtftps.h"

int main()
{
    WvTFTPs tftps("/tftpboot", 30, 30);

    while (tftps.isok())
    {
	if (tftps.select(0))
	    tftps.callback();
    }
    wvcon->print("TFTPs is not okay; aborting.\n");
}
