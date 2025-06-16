#ifndef BOLTPROTOCOL_STRUCTURE_TYPES_H
#define BOLTPROTOCOL_STRUCTURE_TYPES_H

#include <cstdint>
#include <map>
#include <memory>  // For std::shared_ptr in Path
#include <optional>
#include <string>
#include <vector>

#include "bolt_core_types.h"  // For Value, BoltMap, BoltList, PackStreamStructure
// bolt_errors_versions.h might be needed if version checks are done during construction/conversion
#include "bolt_errors_versions.h"

namespace boltprotocol {

    // --- Graph Primitives ---

    // Tag: 0x4E ('N')
    struct BoltNode {
        int64_t id;                               // Field 0: id (Integer)
        std::vector<std::string> labels;          // Field 1: labels (List<String>)
        std::map<std::string, Value> properties;  // Field 2: properties (Map)
        std::optional<std::string> element_id;    // Field 3: element_id (String, Bolt 5.0+)

        // Default constructor
        BoltNode() : id(0) {
        }

        bool operator==(const BoltNode& other) const {
            return id == other.id && labels == other.labels && properties == other.properties && element_id == other.element_id;
        }
    };

    // Tag: 0x52 ('R')
    struct BoltRelationship {
        int64_t id;                                        // Field 0: id (Integer)
        int64_t start_node_id;                             // Field 1: startNodeId (Integer)
        int64_t end_node_id;                               // Field 2: endNodeId (Integer)
        std::string type;                                  // Field 3: type (String)
        std::map<std::string, Value> properties;           // Field 4: properties (Map)
        std::optional<std::string> element_id;             // Field 5: element_id (String, Bolt 5.0+)
        std::optional<std::string> start_node_element_id;  // Field 6: start_node_element_id (String, Bolt 5.0+)
        std::optional<std::string> end_node_element_id;    // Field 7: end_node_element_id (String, Bolt 5.0+)

        BoltRelationship() : id(0), start_node_id(0), end_node_id(0) {
        }

        bool operator==(const BoltRelationship& other) const {
            return id == other.id && start_node_id == other.start_node_id && end_node_id == other.end_node_id && type == other.type && properties == other.properties && element_id == other.element_id && start_node_element_id == other.start_node_element_id &&
                   end_node_element_id == other.end_node_element_id;
        }
    };

    // Tag: 0x72 ('r') - Unbound Relationship (used within Path)
    struct BoltUnboundRelationship {
        int64_t id;                               // Field 0: id (Integer)
        std::string type;                         // Field 1: type (String)
        std::map<std::string, Value> properties;  // Field 2: properties (Map)
        std::optional<std::string> element_id;    // Field 3: element_id (String, Bolt 5.0+)

        BoltUnboundRelationship() : id(0) {
        }

        bool operator==(const BoltUnboundRelationship& other) const {
            return id == other.id && type == other.type && properties == other.properties && element_id == other.element_id;
        }
    };

    // Tag: 0x50 ('P')
    struct BoltPath {
        std::vector<BoltNode> nodes;                // Field 0: nodes (List<Node>)
        std::vector<BoltUnboundRelationship> rels;  // Field 1: rels (List<UnboundRelationship>)
        std::vector<int64_t> indices;               // Field 2: indices (List<Integer>)
        // Note: For Path, nodes and rels are lists of *actual* BoltNode/BoltUnboundRelationship objects,
        // not shared_ptr<PackStreamStructure>. The conversion logic will handle this.

        bool operator==(const BoltPath& other) const {
            return nodes == other.nodes && rels == other.rels && indices == other.indices;
        }
    };

    // --- Temporal Types ---

    // Tag: 0x44 ('D') - Date
    struct BoltDate {
        int64_t days_since_epoch;  // days since Unix epoch (1970-01-01)

        BoltDate(int64_t days = 0) : days_since_epoch(days) {
        }
        bool operator==(const BoltDate& other) const {
            return days_since_epoch == other.days_since_epoch;
        }
    };

    // Tag: 0x54 ('T') - Time (with offset)
    struct BoltTime {
        int64_t nanoseconds_since_midnight;  // nanoseconds since midnight for the given offset
        int32_t tz_offset_seconds;           // offset in seconds from UTC

        BoltTime(int64_t nanos = 0, int32_t offset = 0) : nanoseconds_since_midnight(nanos), tz_offset_seconds(offset) {
        }
        bool operator==(const BoltTime& other) const {
            return nanoseconds_since_midnight == other.nanoseconds_since_midnight && tz_offset_seconds == other.tz_offset_seconds;
        }
    };

    // Tag: 0x74 ('t') - LocalTime
    struct BoltLocalTime {
        int64_t nanoseconds_since_midnight;

        BoltLocalTime(int64_t nanos = 0) : nanoseconds_since_midnight(nanos) {
        }
        bool operator==(const BoltLocalTime& other) const {
            return nanoseconds_since_midnight == other.nanoseconds_since_midnight;
        }
    };

    // Tag: 0x49 ('I') - DateTime (with offset, Bolt 5.0+ and 4.4 with "utc" patch)
    // Replaces Legacy DateTime (tag 0x46 'F')
    struct BoltDateTime {
        int64_t seconds_epoch_utc;      // seconds since Unix epoch (UTC)
        int32_t nanoseconds_of_second;  // nanoseconds within the second (0 to 999,999,999)
        int32_t tz_offset_seconds;      // offset in seconds from UTC for the original instant

        BoltDateTime(int64_t secs = 0, int32_t nanos = 0, int32_t offset = 0) : seconds_epoch_utc(secs), nanoseconds_of_second(nanos), tz_offset_seconds(offset) {
        }
        bool operator==(const BoltDateTime& other) const {
            return seconds_epoch_utc == other.seconds_epoch_utc && nanoseconds_of_second == other.nanoseconds_of_second && tz_offset_seconds == other.tz_offset_seconds;
        }
    };

    // Tag: 0x69 ('i') - DateTimeZoneId (with named zone, Bolt 5.0+ and 4.4 with "utc" patch)
    // Replaces Legacy DateTimeZoneId (tag 0x66 'f')
    struct BoltDateTimeZoneId {
        int64_t seconds_epoch_utc;
        int32_t nanoseconds_of_second;
        std::string tz_id;  // Timezone ID string (e.g., "Europe/Paris")

        BoltDateTimeZoneId(int64_t secs = 0, int32_t nanos = 0, std::string id = "") : seconds_epoch_utc(secs), nanoseconds_of_second(nanos), tz_id(std::move(id)) {
        }
        bool operator==(const BoltDateTimeZoneId& other) const {
            return seconds_epoch_utc == other.seconds_epoch_utc && nanoseconds_of_second == other.nanoseconds_of_second && tz_id == other.tz_id;
        }
    };

    // Tag: 0x64 ('d') - LocalDateTime
    struct BoltLocalDateTime {
        int64_t seconds_epoch_local;  // seconds since Unix epoch, interpreted as local datetime
        int32_t nanoseconds_of_second;

        BoltLocalDateTime(int64_t secs = 0, int32_t nanos = 0) : seconds_epoch_local(secs), nanoseconds_of_second(nanos) {
        }
        bool operator==(const BoltLocalDateTime& other) const {
            return seconds_epoch_local == other.seconds_epoch_local && nanoseconds_of_second == other.nanoseconds_of_second;
        }
    };

    // Tag: 0x45 ('E') - Duration
    struct BoltDuration {
        int64_t months;
        int64_t days;
        int64_t seconds;
        int32_t nanoseconds;  // nanoseconds adjustment for seconds component

        BoltDuration(int64_t m = 0, int64_t d = 0, int64_t s = 0, int32_t ns = 0) : months(m), days(d), seconds(s), nanoseconds(ns) {
        }
        bool operator==(const BoltDuration& other) const {
            return months == other.months && days == other.days && seconds == other.seconds && nanoseconds == other.nanoseconds;
        }
    };

    // --- Spatial Types ---

    // Tag: 0x58 ('X') - Point2D
    struct BoltPoint2D {
        uint32_t srid;  // Spatial Reference System Identifier
        double x;
        double y;

        BoltPoint2D(uint32_t id = 0, double px = 0.0, double py = 0.0) : srid(id), x(px), y(py) {
        }
        bool operator==(const BoltPoint2D& other) const {
            return srid == other.srid && x == other.x && y == other.y;
        }
    };

    // Tag: 0x59 ('Y') - Point3D
    struct BoltPoint3D {
        uint32_t srid;
        double x;
        double y;
        double z;

        BoltPoint3D(uint32_t id = 0, double px = 0.0, double py = 0.0, double pz = 0.0) : srid(id), x(px), y(py), z(pz) {
        }
        bool operator==(const BoltPoint3D& other) const {
            return srid == other.srid && x == other.x && y == other.y && z == other.z;
        }
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_STRUCTURE_TYPES_H