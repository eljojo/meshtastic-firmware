#include "NaraModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include "graphics/ScreenFonts.h"

#include <assert.h>

meshtastic_MeshPacket *NaraModule::allocReply()
{
    assert(currentRequest); // should always be !NULL
#ifdef DEBUG_PORT
    auto req = *currentRequest;
    auto &p = req.decoded;
    // The incoming message is in p.payload
    LOG_INFO("NARA Received message from=0x%0x, id=%d, msg=%.*s\n", req.from, req.id, p.payload.size, p.payload.bytes);
#endif

    screen->print("Sending reply\n");

    const char *replyStr = "Message Received";
    auto reply = allocDataPacket();                 // Allocate a packet for sending
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);

    return reply;
}

void NaraModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Nara");
    display->setFont(FONT_SMALL);
    display->drawString(x, y += _fontHeight(FONT_MEDIUM), "Hello World");
    return;
}

bool NaraModule::wantUIFrame()
{
    return true;
}

int32_t NaraModule::runOnce()
{
    if (firstTime) {
      firstTime = 0;
      LOG_DEBUG("runOnce on NaraModule for the first time\n");
    } else {
      LOG_DEBUG("other NaraModule runs\n");
    }
    return 60 * 1000; // run again in 60 seconds
}
