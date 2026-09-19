#ifndef SENSECAP_DISPLAY_BUS_H
#define SENSECAP_DISPLAY_BUS_H

#ifdef __cplusplus

class Arduino_DataBus;

Arduino_DataBus *sensecapInitBus();

#define SENSECAP_RGB_INIT_BUS sensecapInitBus()

#endif

#endif // SENSECAP_DISPLAY_BUS_H
