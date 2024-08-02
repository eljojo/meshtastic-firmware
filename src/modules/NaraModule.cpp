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
#define MAX_CLOSEST_NODES 3           // Maximum number of closest nodes to display
#define NUM_ZEROES 4                  // for hashcash


bool NaraModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NaraMessage *nara_message)
{
  LOG_INFO(
      "NARA Received message from=0x%0x, id=%d, haiku_text=%s, haiku_signature=%d, msg_type=%d\n",
      mp.from, mp.id, nara_message->haiku.text, nara_message->haiku.signature, nara_message->type
  );

  if (nara_message->haiku.signature > 0) {
    if (crypto->checkHashcash(nara_message->haiku.text, nara_message->haiku.signature, NUM_ZEROES)) {
      LOG_DEBUG("NARA Signature verified\n");
    } else {
      LOG_INFO("NARA Signature verification failed\n");
      return false;
    }
  }

  if (nara_message->type == meshtastic_NaraMessage_MessageType_GREETING) {
    if (naraDatabase.find(mp.from) != naraDatabase.end()) {
      screenLog = "0x" + String(mp.from, HEX) + " says hi again";
      naraDatabase[mp.from].status = GREETING_RECEIVED;
    } else {
      screenLog = "0x" + String(mp.from, HEX) + " says hi";
      naraDatabase[mp.from] = {GREETING_RECEIVED};
    }

    return true;
  } else if (nara_message->type == meshtastic_NaraMessage_MessageType_PRESENT) {
    if (naraDatabase.find(mp.from) != naraDatabase.end()) {
      if(naraDatabase[mp.from].status == GREETING_SENT) {
        naraDatabase[mp.from].status = PRESENT_RECEIVED;
        screenLog = "0x" + String(mp.from, HEX) + " sent a haiku";
      }else{
        // screenLog = "0x" + String(mp.from, HEX) + " sent a new haiku";
        LOG_WARN("Nara sent valid present but we weren't expecting it. current_status=%d\n", naraDatabase[mp.from].status);
      }
    } else {
      screenLog = String("unexpected haiku from 0x") + String(mp.from, HEX);
      LOG_WARN("Nara sent valid present but we hadn't seen greeting\n");
      // naraDatabase[mp.from] = {PRESENT_RECEIVED};
    }

    return true;
  }

  return false;
}

bool NaraModule::sendGreeting(NodeNum dest)
{
    meshtastic_NaraMessage_Haiku haiku = meshtastic_NaraMessage_Haiku_init_default;
    strncpy(haiku.text, hashMessage.c_str(), sizeof(hashMessage));

    meshtastic_NaraMessage nara_message = meshtastic_NaraMessage_init_default;
    nara_message.type = meshtastic_NaraMessage_MessageType_GREETING;
    nara_message.has_haiku = true;
    nara_message.haiku = haiku;

    meshtastic_MeshPacket *p = allocDataProtobuf(nara_message);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

    LOG_INFO("NARA Sending greeting to=0x%0x, id=%d, haiku_text=%s,haiku_signature=%d,msg_type=%d\n", p->to, p->id, nara_message.haiku.text, nara_message.haiku.signature, nara_message.type);
    service.sendToMesh(p, RX_SRC_LOCAL, true);

    screenLog = "found 0x" + String(p->to, HEX);

    return true;
}

bool NaraModule::sendPresent(NodeNum dest)
{
    // screenLog = "prep gift for 0x" + String(dest, HEX);

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

    screenLog = "friends with 0x" + String(p->to, HEX);

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
      entry.second.status = GREETING_SENT;
      sendGreeting(entry.first);
      return true;
    }else if (entry.second.status == GREETING_RECEIVED || entry.second.status == PRESENT_RECEIVED) {
      LOG_INFO("node %0x is waiting for a present, sending now\n", entry.first);
      entry.second.status = PRESENT_SENT;
      sendPresent(entry.first);
      return true;
    }
  }
  return false;
}

int32_t NaraModule::runOnce()
{
  if (firstTime) {
    firstTime = 0;
    LOG_DEBUG("NaraModule initialized\n");

    // int signature = crypto->performHashcash(owner.long_name, NUM_ZEROES);
    // hashMessage = crypto->getHashString(owner.long_name, signature);
    // LOG_INFO("found hash for \"%s\": %s\n",owner.long_name, hashMessage.c_str());
    hashMessage = String("Hello ") + String(owner.long_name);

    screenLog = String("Hello ") + String(owner.long_name);
    return 10000 + random(20000); // run again in 10-30 seconds
  }

  updateNodeCount();
  messageNextNode();

  return 10 * 1000 + random(10 * 1000);
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

  closestNodes = getClosestNodeNames(MAX_CLOSEST_NODES);
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
    String nodeNames = "w/ ";
    int count = 0;
    int currentLineLength = 0;

    for (const auto &nodePair : nodeList) {
        if (count >= maxNodes) break;

        meshtastic_NodeInfoLite* node = nodePair.second;
        if (!node->has_user) continue;
        if (strcmp(node->user.short_name, "") == 0) continue;

        String nodeName = node->user.short_name;

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

    if (nodeNames == "w/ ") {
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
  int points = 0;
  int naraSeen = 0;
  for (auto& entry : naraDatabase) {
    if (entry.second.status == GREETING_SENT) {
      points += 1;
    } else if (entry.second.status == GREETING_RECEIVED || entry.second.status == PRESENT_RECEIVED) {
      points += 2;
      naraSeen += 1;
    } else if (entry.second.status == PRESENT_SENT) {
      points += 3;
      naraSeen += 1;
    }
  }

  return String(naraSeen) + " nara seen | " + String(points) + " points";
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
