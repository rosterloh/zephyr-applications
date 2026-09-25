/*
 * ROS 2 message types used by rasprover, in the X-macro form picoserdes
 * expects. Passed to the build as USER_TYPE_FILE (see CMakeLists.txt).
 *
 * picoserdes generates a serialize and a deserialize function for *every*
 * entry in MSG_LIST, so this is a hand-trimmed list rather than upstream's
 * examples/example_types.h (1385 lines, ~180 types). Entries were copied
 * verbatim from that file so the type names and RIHS01 hashes match what
 * rmw_zenoh advertises; regenerate with Pico-ROS's tools/type-gen if a
 * message is added.
 *
 * Order matters: a compound type's fields expand into a C struct, so a type
 * must appear after every type it embeds.
 *
 * SRV_LIST is empty but must still be defined -- picoserdes.h only supplies
 * its own empty fallbacks when USER_TYPE_FILE is absent.
 */

#ifndef RASPROVER_TYPES_H
#define RASPROVER_TYPES_H

/* clang-format off */

#define MSG_LIST(BTYPE, CTYPE, TTYPE, FIELD, ARRAY, SEQUENCE) \
    CTYPE(ros_Time, \
        "builtin_interfaces::msg::dds_::Time", \
        "b106235e25a4c5ed35098aa0a61a3ee9c9b18d197f398b0e4206cea9acf9c197", \
        FIELD(int32_t, sec) \
        FIELD(uint32_t, nanosec) \
    ) \
    CTYPE(ros_Header, \
        "std_msgs::msg::dds_::Header", \
        "f49fb3ae2cf070f793645ff749683ac6b06203e41c891e17701b1cb597ce6a01", \
        FIELD(ros_Time, stamp) \
        FIELD(rstring, frame_id) \
    ) \
    CTYPE(ros_Vector3, \
        "geometry_msgs::msg::dds_::Vector3", \
        "cc12fe83e4c02719f1ce8070bfd14aecd40f75a96696a67a2a1f37f7dbb0765d", \
        FIELD(double, x) \
        FIELD(double, y) \
        FIELD(double, z) \
    ) \
    CTYPE(ros_Twist, \
        "geometry_msgs::msg::dds_::Twist", \
        "9c45bf16fe0983d80e3cfe750d6835843d265a9a6c46bd2e609fcddde6fb8d2a", \
        FIELD(ros_Vector3, linear) \
        FIELD(ros_Vector3, angular) \
    ) \
    CTYPE(ros_BatteryState, \
        "sensor_msgs::msg::dds_::BatteryState", \
        "4bee5dfce981c98faa6828b868307a0a73f992ed0789f374ee96c8f840e69741", \
        FIELD(ros_Header, header) \
        FIELD(float, voltage) \
        FIELD(float, temperature) \
        FIELD(float, current) \
        FIELD(float, charge) \
        FIELD(float, capacity) \
        FIELD(float, design_capacity) \
        FIELD(float, percentage) \
        FIELD(uint8_t, power_supply_status) \
        FIELD(uint8_t, power_supply_health) \
        FIELD(uint8_t, power_supply_technology) \
        FIELD(bool, present) \
        SEQUENCE(float, cell_voltage) \
        SEQUENCE(float, cell_temperature) \
        FIELD(rstring, location) \
        FIELD(rstring, serial_number) \
    ) \
    CTYPE(ros_JointState, \
        "sensor_msgs::msg::dds_::JointState", \
        "a13ee3a330e346c9d87b5aa18d24e11690752bd33a0350f11c5882bc9179260e", \
        FIELD(ros_Header, header) \
        SEQUENCE(rstring, name) \
        SEQUENCE(double, position) \
        SEQUENCE(double, velocity) \
        SEQUENCE(double, effort) \
    )

#define SRV_LIST(SRV, REQUEST, REPLY, FIELD, ARRAY, SEQUENCE)

/* clang-format on */

#endif /* RASPROVER_TYPES_H */
