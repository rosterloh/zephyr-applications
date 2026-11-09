"""Move a cube in RViz from the joystick's TwistStamped, to check the link before driving an arm.

Run in a ROS 2 env on the controller's domain and router, e.g. from ~/eit/eit_robot:
    pixi run -e rebot python <this file> & pixi run -e rebot rviz2 -d <dir>/cube.rviz
"""

import math

import rclpy
from geometry_msgs.msg import Point, TwistStamped
from rclpy.node import Node
from rclpy.time import Time
from std_msgs.msg import ColorRGBA
from visualization_msgs.msg import Marker

TOPIC = "/servo_node/delta_twist_cmds"
RATE_HZ = 50
TIMEOUT_S = 0.15  # same as the rebot Servo config, so a gap here is a gap there
LIMIT_M = 1.0
HALF = (0.1, 0.1, 0.1)
# +x/-x, +y/-y, +z/-z: RViz axis colours, complements on the opposite faces.
FACE_COLOURS = (
    ((1, 0, 0), (0, 1, 1)),
    ((0, 1, 0), (1, 0, 1)),
    ((0, 0, 1), (1, 1, 0)),
)


def cube_triangles():
    """Points and per-vertex colours for a TRIANGLE_LIST cube, outward winding."""
    points, colours = [], []
    for a in range(3):
        b, c = (a + 1) % 3, (a + 2) % 3
        for s, rgb in zip((1, -1), FACE_COLOURS[a], strict=True):
            corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
            if s < 0:
                corners.reverse()
            quad = []
            for u, v in corners:
                p = [0.0, 0.0, 0.0]
                p[a], p[b], p[c] = s * HALF[a], u * HALF[b], v * HALF[c]
                quad.append(Point(x=p[0], y=p[1], z=p[2]))
            for i in (0, 1, 2, 0, 2, 3):
                points.append(quad[i])
                colours.append(ColorRGBA(r=float(rgb[0]), g=float(rgb[1]), b=float(rgb[2]), a=1.0))
    return points, colours


def rotate(q, w, dt):
    """Apply world-frame angular velocity w (rad/s) for dt to quaternion q (x, y, z, w)."""
    angle = math.sqrt(sum(c * c for c in w)) * dt
    if angle == 0.0:
        return q
    s = math.sin(angle / 2) / (angle / dt)
    dx, dy, dz, dw = w[0] * s, w[1] * s, w[2] * s, math.cos(angle / 2)
    x, y, z, qw = q
    r = (
        dw * x + dx * qw + dy * z - dz * y,
        dw * y - dx * z + dy * qw + dz * x,
        dw * z + dx * y - dy * x + dz * qw,
        dw * qw - dx * x - dy * y - dz * z,
    )
    n = math.sqrt(sum(c * c for c in r))
    return tuple(c / n for c in r)


class Cube(Node):
    def __init__(self):
        super().__init__("joystick_cube")
        self.pos = [0.0, 0.0, 0.0]
        self.q = (0.0, 0.0, 0.0, 1.0)
        self.twist = None
        self.received = None
        self.count = 0
        self.points, self.colours = cube_triangles()
        self.pub = self.create_publisher(Marker, "cube", 1)
        self.create_subscription(TwistStamped, TOPIC, self.on_twist, 10)
        self.create_timer(1.0 / RATE_HZ, self.step)
        self.create_timer(1.0, self.report)

    def on_twist(self, msg):
        self.twist = msg
        self.received = self.get_clock().now()
        self.count += 1

    def report(self):
        if self.count:
            age = (self.received - Time.from_msg(self.twist.header.stamp)).nanoseconds / 1e9
            t = self.twist.twist
            self.get_logger().info(
                f"{self.count} msg/s, stamp age {age * 1000:.0f} ms, "
                f"lin ({t.linear.x:.2f} {t.linear.y:.2f} {t.linear.z:.2f}) "
                f"ang ({t.angular.x:.2f} {t.angular.y:.2f} {t.angular.z:.2f})"
            )
        self.count = 0

    def step(self):
        dt = 1.0 / RATE_HZ
        now = self.get_clock().now()
        if self.twist and (now - self.received).nanoseconds / 1e9 < TIMEOUT_S:
            lin, ang = self.twist.twist.linear, self.twist.twist.angular
            for i, v in enumerate((lin.x, lin.y, lin.z)):
                self.pos[i] = max(-LIMIT_M, min(LIMIT_M, self.pos[i] + v * dt))
            self.q = rotate(self.q, (ang.x, ang.y, ang.z), dt)

        m = Marker(type=Marker.TRIANGLE_LIST, action=Marker.ADD, points=self.points, colors=self.colours)
        m.header.frame_id = "world"
        m.header.stamp = now.to_msg()
        m.pose.position.x, m.pose.position.y, m.pose.position.z = self.pos
        o = m.pose.orientation
        o.x, o.y, o.z, o.w = self.q
        m.scale.x = m.scale.y = m.scale.z = 1.0
        m.color.a = 1.0
        self.pub.publish(m)


def main():
    rclpy.init()
    rclpy.spin(Cube())


if __name__ == "__main__":
    main()
