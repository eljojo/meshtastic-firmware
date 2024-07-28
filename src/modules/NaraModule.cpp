#include "NaraModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include "graphics/ScreenFonts.h"

#include <assert.h>
#include <vector>

NaraModule *naraModule;

#define NUM_ONLINE_SECS (60 * 60 * 2) // 2 hours to consider someone online
#define MAX_LINE_LENGTH 26            // Maximum characters per line
#define MAX_CLOSEST_NODES 4           // Maximum number of closest nodes to display

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
  display->drawString(x, y, owner.long_name);
  y += _fontHeight(FONT_MEDIUM);
  display->setFont(FONT_SMALL);
  display->drawString(x, y, getNaraMessage(y));

  String closestNodes = getClosestNodeNames(MAX_CLOSEST_NODES);
  int splitIndex = closestNodes.indexOf('\n');

  y += _fontHeight(FONT_SMALL);
  if (splitIndex != -1) {
    display->drawString(x, y, closestNodes.substring(0, splitIndex));
    y += _fontHeight(FONT_SMALL);
    display->drawString(x, y, closestNodes.substring(splitIndex + 1));
  } else {
    display->drawString(x, y, closestNodes);
  }

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

  // Update node count
  updateNodeCount();

  return 10 * 1000; // run again in 10 seconds
}

void NaraModule::updateNodeCount()
{
  nodeCount = 0;
  NodeNum localNodeNum = nodeDB->getNodeNum();

  for (int i = 0; i < nodeDB->getNumMeshNodes(); i++) {
    meshtastic_NodeInfoLite* node = nodeDB->getMeshNodeByIndex(i);

    if (!node->has_user) continue;
    if (strcmp(node->user.long_name, "") == 0) continue;

    if (node->num != localNodeNum && sinceLastSeen(node) < NUM_ONLINE_SECS && !node->via_mqtt) {
      nodeCount++;
    }
  }

  LOG_DEBUG("[NARA] nodeCount=%u\n", nodeCount);
}

String NaraModule::getClosestNodeNames(int maxNodes)
{
    std::vector<std::pair<int, meshtastic_NodeInfoLite*>> nodeList;
    NodeNum localNodeNum = nodeDB->getNodeNum();

    for (int i = 0; i < nodeDB->getNumMeshNodes(); i++) {
        meshtastic_NodeInfoLite* node = nodeDB->getMeshNodeByIndex(i);
        if (node->num != localNodeNum && sinceLastSeen(node) < NUM_ONLINE_SECS && !node->via_mqtt) {
            nodeList.emplace_back(node->snr, node);
        }
    }

    // Sort nodes by SNR in descending order
    std::sort(nodeList.begin(), nodeList.end(), [](const std::pair<int, meshtastic_NodeInfoLite*>& a, const std::pair<int, meshtastic_NodeInfoLite*>& b) {
        return a.first > b.first;
    });

    // Generate the string with the closest node names
    String nodeNames = "feat ";
    int count = 0;
    int currentLineLength = 0;

    for (const auto &nodePair : nodeList) {
        if (count >= maxNodes) break;

        meshtastic_NodeInfoLite* node = nodePair.second;
        if (!node->has_user) continue;
        if (strcmp(node->user.long_name, "") == 0) continue;

        String nodeName = node->user.long_name;

        if (currentLineLength + nodeName.length() + 2 > MAX_LINE_LENGTH) {  // +2 for ", "
            nodeNames += "\n";
            currentLineLength = 0;
        }

        if (currentLineLength > 0) {
            nodeNames += ", ";
            currentLineLength += 2;
        }

        nodeNames += nodeName;
        currentLineLength += nodeName.length();
        count++;
    }

    if (nodeNames == "feat ") {
        nodeNames = "No nodes";
    }

    return nodeNames;
}

String NaraModule::getShortMessage()
{
  if (nodeCount <= 2) {
    return ":/";
  } else if (nodeCount <= 5) {
    return ":)";
  } else if (nodeCount <= 9) {
    return ":D";
  } else {
    return "xD";
  }
}

String NaraModule::getLongMessage()
{
  if (nodeCount <= 2) {
    return "it's quiet here...";
  } else if (nodeCount <= 5) {
    return "oh, hello there";
  } else if (nodeCount <= 9) {
    return "let's get this party started!";
  } else {
    return "EVERYONE IS HERE";
  }
}

String NaraModule::getNaraMessage(int16_t y)
{
  if (y < FONT_HEIGHT_SMALL) {
    return getShortMessage();
  } else {
    return getLongMessage();
  }
}
