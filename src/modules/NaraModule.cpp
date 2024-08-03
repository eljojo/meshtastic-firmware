#include "NaraEntry.h"
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


bool NaraModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NaraMessage *nm)
{
  LOG_INFO(
      "NARA Received Haiku from=0x%0x, id=0x%0x, text=\"%s\", signature=%d, msg_type=%d\n",
      mp.from, mp.id, nm->haiku.text, nm->haiku.signature, nm->type
  );

  if (nm->haiku.signature > 0) {
    if (crypto->checkHashcash(nm->haiku.text, nm->haiku.signature, NUM_ZEROES)) {
      LOG_DEBUG("NARA Haiku Signature verified\n");
    } else {
      LOG_WARN("NARA Haiku Signature verification failed\n");
      setLog("received invalid haiku");
      return true; // ignore this message
    }
  }

  if (naraDatabase.find(mp.from) == naraDatabase.end()) {
    LOG_DEBUG("added entry to database\n");
    naraDatabase[mp.from] = NaraEntry(mp.from, UNCONTACTED);
  }

  NaraEntry &entry = naraDatabase[mp.from];
  entry.handleMeshPacket(mp, nm);

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

void NaraModule::setLog(String log)
{
  LOG_INFO("%s\n", log.c_str());
  screenLog = log;
}

bool NaraModule::wantUIFrame()
{
  return true;
}

bool NaraModule::messageNextNode()
{
  for (auto& entry : naraDatabase) {
    if(entry.second.processNextStep()) {
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
    return random(5 * 1000, 20 * 1000); // run again in 5-10 seconds
  }

  updateNodeCount();
  messageNextNode();

  return random(5 * 1000, 15 * 1000); // run again in 5-10 seconds
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

    //if (node->num != localNodeNum && sinceLastSeen(node) < NUM_ONLINE_SECS && !node->via_mqtt) {
    if (node->num != localNodeNum && !node->via_mqtt) {
      nodeCount++;

      // Maintain the NaraEntry database
      if (naraDatabase.find(node->num) == naraDatabase.end()) {
        naraDatabase[node->num] = NaraEntry(node->num, UNCONTACTED);
      }
    }
  }

  //LOG_DEBUG("[NARA] nodeCount=%u\n", nodeCount);

  closestNodes = getClosestNodeNames(MAX_CLOSEST_NODES);
}

bool NaraModule::sendHaiku(NodeNum dest, char* haikuText, _meshtastic_NaraMessage_MessageType messageType, int signature = 0) {
  meshtastic_NaraMessage_Haiku haiku = meshtastic_NaraMessage_Haiku_init_default;
  strncpy(haiku.text, haikuText, sizeof(haiku.text) - 1);
  haiku.text[sizeof(haiku.text) - 1] = '\0';

  haiku.signature = signature;

  meshtastic_NaraMessage nm = meshtastic_NaraMessage_init_default;
  nm.type = messageType;
  nm.has_haiku = true;
  nm.haiku = haiku;

  meshtastic_MeshPacket *p = allocDataProtobuf(nm);
  p->to = dest;
  p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

  LOG_INFO("NARA Sending haiku to=0x%0x, id=0x%0x, haiku_text=\"%s\",haiku_signature=%d,msg_type=%d\n", p->to, p->id, nm.haiku.text, nm.haiku.signature, nm.type);
  service.sendToMesh(p, RX_SRC_LOCAL, true);

  return true;
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
    // call getPoints and increase nara seen if larger than 1
    points += entry.second.getPoints();
    if (entry.second.getPoints() > 1) {
      naraSeen++;
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
