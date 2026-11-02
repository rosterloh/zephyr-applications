#ifndef TELEOP_TYPES_H
#define TELEOP_TYPES_H

/* picoserdes MSG_LIST: only the types this app publishes. Names and RIHS01
 * hashes copied from Pico-ROS examples/example_types.h.
 */

/* clang-format off */

#define MSG_LIST(BTYPE, CTYPE, TTYPE, FIELD, ARRAY, SEQUENCE) \
    BTYPE(ros_Bool, \
        "std_msgs::msg::dds_::Bool", \
        "feb91e995ff9ebd09c0cb3d2aed18b11077585839fb5db80193b62d74528f6c9", \
        bool \
    ) \
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
    CTYPE(ros_TwistStamped, \
        "geometry_msgs::msg::dds_::TwistStamped", \
        "5f0fcd4f81d5d06ad9b4c4c63e3ea51b82d6ae4d0558f1d475229b1121db6f64", \
        FIELD(ros_Header, header) \
        FIELD(ros_Twist, twist) \
    )

#define SRV_LIST(SRV, REQUEST, REPLY, FIELD, ARRAY, SEQUENCE)

/* clang-format on */

#endif /* TELEOP_TYPES_H */
