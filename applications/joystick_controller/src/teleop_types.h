#ifndef TELEOP_TYPES_H
#define TELEOP_TYPES_H

/* picoserdes MSG_LIST: only the types this app publishes. Names and RIHS01
 * hashes copied from applications/rasprover/src/rasprover_types.h.
 */

/* clang-format off */

#define MSG_LIST(BTYPE, CTYPE, TTYPE, FIELD, ARRAY, SEQUENCE) \
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
    )

#define SRV_LIST(SRV, REQUEST, REPLY, FIELD, ARRAY, SEQUENCE)

/* clang-format on */

#endif /* TELEOP_TYPES_H */
