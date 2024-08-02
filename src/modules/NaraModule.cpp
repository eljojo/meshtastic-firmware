#include "NaraModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include "graphics/ScreenFonts.h"
#include "CryptoEngine.h"

#include <assert.h>
#include <vector>

NaraModule *naraModule;

#define NUM_ONLINE_SECS (60 * 60 * 2) // 2 hours to consider someone online
#define MAX_LINE_LENGTH 26            // Maximum characters per line
#define MAX_CLOSEST_NODES 4           // Maximum number of closest nodes to display
#define NUM_ZEROES 4                  // for hashcash

meshtastic_MeshPacket *NaraModule::allocReply()
{
  assert(currentRequest); // should always be !NULL

  // Copy the payload of the current request
  auto req = *currentRequest;
  const auto &p = req.decoded;

#ifdef DEBUG_PORT
  // The incoming message is in p.payload
  LOG_INFO("NARA Received message from=0x%0x, id=%d, msg=%.*s\n", req.from, req.id, p.payload.size, p.payload.bytes);
#endif

  meshtastic_NaraMessage scratch;
  memset(&scratch, 0, sizeof(scratch));
  pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_NaraMessage_msg, &scratch);
  meshtastic_NaraMessage *updated = NULL;
  updated = &scratch;

  /* printRoute(updated, req.from, req.to); */

  screen->print("Sending reply\n");

  // Create a MeshPacket with this payload and set it as the reply
  meshtastic_MeshPacket *reply = allocDataProtobuf(*updated);

  // const char *replyStr = "Message Received";
  // reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
  // memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);

  return reply;
}

bool NaraModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NaraMessage *nara_message)
{
  LOG_INFO("NARA Received message from=0x%0x, id=%d, haiku_text=%s, haiku_signature=%d, msg_type=%d\n", mp.from, mp.id, nara_message->haiku.text, nara_message->haiku.signature, nara_message->type);

  if (nara_message->haiku.signature > 0) {
    if (crypto->checkHashcash(nara_message->haiku.text, nara_message->haiku.signature, NUM_ZEROES)) {
      LOG_INFO("NARA Signature verified\n");
    } else {
      LOG_INFO("NARA Signature verification failed\n");
      return false;
    }
  }

  if (nara_message->type == meshtastic_NaraMessage_MessageType_GREETING) {
    screenLog = "0x" + String(mp.from, HEX) + " says hi";

    // Update the NaraEntry database, setting the status to GREETING_RECEIVED
    if (naraDatabase.find(mp.from) != naraDatabase.end()) {
      naraDatabase[mp.from].status = GREETING_RECEIVED;
    } else {
      naraDatabase[mp.from] = {GREETING_RECEIVED};
    }

    return true;
  } else if (nara_message->type == meshtastic_NaraMessage_MessageType_PRESENT) {
    screenLog = "0x" + String(mp.from, HEX) + " sent a gift";

    // Update the NaraEntry database, setting the status to PRESENT_RECEIVED
    if (naraDatabase.find(mp.from) != naraDatabase.end()) {
      naraDatabase[mp.from].status = PRESENT_RECEIVED;
    } else {
      naraDatabase[mp.from] = {PRESENT_RECEIVED};
    }

    return true;
  }

    /* // Only handle a response */
    /* if (mp.decoded.request_id) { */
    /*     printRoute(r, mp.to, mp.from); */
    /* } */

    return true;
}

bool NaraModule::sendGreeting(NodeNum dest)
{
    meshtastic_NaraMessage_Haiku haiku = meshtastic_NaraMessage_Haiku_init_default;
    strncpy(haiku.text, hashMessage.c_str(), sizeof(hashMessage));
    // haiku.signature = 123;

    meshtastic_NaraMessage nara_message = meshtastic_NaraMessage_init_default;
    nara_message.type = meshtastic_NaraMessage_MessageType_GREETING;
    nara_message.has_haiku = true;
    nara_message.haiku = haiku;

    meshtastic_MeshPacket *p = allocDataProtobuf(nara_message);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

    LOG_INFO("NARA Sending greeting to=0x%0x, id=%d, haiku_text=%s,haiku_signature=%d,msg_type=%d\n", p->to, p->id, nara_message.haiku.text, nara_message.haiku.signature, nara_message.type);
    service.sendToMesh(p, RX_SRC_LOCAL, true);

    screenLog = "greeting 0x" + String(p->to, HEX);

    return true;
}

bool NaraModule::sendPresent(NodeNum dest)
{
    meshtastic_NaraMessage_Haiku haiku = meshtastic_NaraMessage_Haiku_init_default;
    strncpy(haiku.text, hashMessage.c_str(), sizeof(hashMessage));
    haiku.signature = crypto->performHashcash(haiku.text, NUM_ZEROES);

    meshtastic_NaraMessage nara_message = meshtastic_NaraMessage_init_default;
    nara_message.type = meshtastic_NaraMessage_MessageType_PRESENT;
    nara_message.has_haiku = true;
    nara_message.haiku = haiku;

    meshtastic_MeshPacket *p = allocDataProtobuf(nara_message);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

    LOG_INFO("NARA Sending present to=0x%0x, id=%d, haiku_text=%s,haiku_signature=%d,msg_type=%d\n", p->to, p->id, nara_message.haiku.text, nara_message.haiku.signature, nara_message.type);
    service.sendToMesh(p, RX_SRC_LOCAL, true);

    screenLog = "gift for 0x" + String(p->to, HEX);

    return true;
}

void NaraModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(FONT_SMALL);
  // display->drawString(x, y, owner.long_name);
  display->drawString(x, y, screenLog);
  y += _fontHeight(FONT_SMALL);
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

bool NaraModule::messageNextNode()
{
  for (auto& entry : naraDatabase) {
    if (entry.second.status == UNCONTACTED) {
      LOG_INFO("node %0x is UNCONTACTED, messaging now\n", entry.first);
      sendGreeting(entry.first);
      entry.second.status = GREETING_SENT;
      return true;
    }else if (entry.second.status == GREETING_RECEIVED) {
      LOG_INFO("node %0x is waiting for a present, sending now\n", entry.first);
      sendPresent(entry.first);
      entry.second.status = PRESENT_SENT;
      return true;
    }
  }
  return false;
}

int32_t NaraModule::runOnce()
{
  updateNodeCount();

  if (firstTime) {
    firstTime = 0;
    LOG_DEBUG("runOnce on NaraModule for the first time\n");

    screenLog = "Hello Nara";

    int signature = crypto->performHashcash(owner.long_name, NUM_ZEROES);
    hashMessage = crypto->getHashString(owner.long_name, signature);

    LOG_INFO("found hash for \"%s\": %s\n",owner.long_name, hashMessage.c_str());

    screenLog = String("Hello ") + String(owner.long_name);

    /* char buffer[128]; */
    /* snprintf(buffer, sizeof(buffer), "c: %d", signature); */
    /* hashMessage = String(buffer); */
  } else {
    LOG_DEBUG("other NaraModule runs\n");

    messageNextNode();
  }

  return 10 * 1000; // run again in 10 seconds
}

// used for debugging
const NodeNum NARA2 = 0x336a3370;
const NodeNum NARA6 = 0x4359109c;

void NaraModule::updateNodeCount()
{
  nodeCount = 0;
  NodeNum localNodeNum = nodeDB->getNodeNum();

  for (int i = 0; i < nodeDB->getNumMeshNodes(); i++) {
    meshtastic_NodeInfoLite* node = nodeDB->getMeshNodeByIndex(i);

    if (!node->has_user) continue;
    if (strcmp(node->user.long_name, "") == 0) continue;

    // only enable for these debug nodes
    if (node->num != NARA2 && node->num != NARA6) continue;

    if (node->num != localNodeNum && sinceLastSeen(node) < NUM_ONLINE_SECS && !node->via_mqtt) {
      nodeCount++;

      // Maintain the NaraEntry database
      if (naraDatabase.find(node->num) == naraDatabase.end()) {
        naraDatabase[node->num] = {UNCONTACTED};
      }
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

NaraModule::NaraModule() : concurrency::OSThread("NaraModule"), ProtobufModule("nara", meshtastic_PortNum_NARA_APP, &meshtastic_NaraMessage_msg) {
  ourPortNum = meshtastic_PortNum_NARA_APP;
}
