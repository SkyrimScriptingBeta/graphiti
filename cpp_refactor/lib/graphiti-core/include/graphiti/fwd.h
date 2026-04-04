#pragma once

#include <chrono>
#include <string>

namespace graphiti {

using TimePoint = std::chrono::system_clock::time_point;

struct EntityNode;
struct EpisodicNode;
struct CommunityNode;
struct SagaNode;
struct EntityEdge;
struct EpisodicEdge;
struct CommunityEdge;
struct HasEpisodeEdge;
struct NextEpisodeEdge;

struct GraphitiConfig;
struct LLMConfig;
struct EmbedderConfig;
struct SearchConfig;
struct SearchResults;
struct AddEpisodeResult;

class Graphiti;

} // namespace graphiti
