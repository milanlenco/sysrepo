#!/usr/bin/env python
# -*- coding: utf-8 -*-
__author__ = "Rastislav Szabo <raszabo@cisco.com>, Lukas Macko <lmacko@cisco.com>"
__copyright__ = "Copyright 2016, Cisco Systems, Inc."
__license__ = "Apache 2.0"

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# sysrepod and notifications_test_app must be in PATH

from ConcurrentHelpers import *
from SysrepoWrappers import *
from random import randint
import signal
import os
import subprocess
import TestModule

class NotificationTester(SysrepoTester):

    def cleanup(self):
        if self.filename:
            os.unlink(self.filename)

    def subscribeStep(self, xpath):
        self.filename = "notifications_test_" + str(randint(0,9999))
        self.process = subprocess.Popen(["notifications_test_app", xpath, self.filename])
        self.report_pid(self.process.pid)
        # wait for running data file to be copied
        time.sleep(0.1)

    def cancelSubscriptionStep(self):
        os.kill(self.process.pid, signal.SIGINT)
        self.process.wait()

    def checkNotificationStep(self, expected):
        with open(self.filename, "r") as f:
            self.notifications = []
            for line in f:
                self.notifications.append(line.split("|"))

        for n in self.notifications:
            print n

        self.tc.assertEqual(len(expected), len(self.notifications))
        for i in range(len(expected)):
            self.tc.assertEqual(self.notifications[i][0], expected[i][0])
            self.tc.assertEqual(self.notifications[i][1], expected[i][1])



class SysrepodTester(SysrepoTester):

    def startDaemonStep(self):
        self.process = subprocess.Popen(["sysrepod", "-d"])
        self.report_pid(self.process.pid)
        time.sleep(0.1)


    def stopDaemonStep(self):
        os.kill(self.process.pid, signal.SIGTERM)
        self.process.wait()


class SubscriptionTest(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        TestModule.create_ietf_interfaces()

    def test_SubscribeUnsubscribe(self):
        tm = TestManager()

        srd = SysrepodTester("Srd")
        tester = SysrepoTester("Tester", SR_DS_RUNNING, SR_CONN_DAEMON_REQUIRED, False)
        subscriber = NotificationTester("Subscriber")
        subscriber2 = NotificationTester("Subscriber2")


        srd.add_step(srd.startDaemonStep)
        tester.add_step(tester.waitStep)
        subscriber.add_step(subscriber.waitStep)
        subscriber2.add_step(subscriber2.waitStep)

        srd.add_step(srd.waitStep)
        tester.add_step(tester.restartConnection)
        subscriber.add_step(subscriber.waitStep)
        subscriber2.add_step(subscriber2.waitStep)

        srd.add_step(srd.waitStep)
        tester.add_step(tester.waitStep)
        subscriber.add_step(subscriber.subscribeStep, "/ietf-interfaces:interfaces")
        subscriber2.add_step(subscriber2.subscribeStep, "/ietf-interfaces:interfaces/interface/ietf-ip:ipv4/address")

        srd.add_step(srd.waitStep)
        tester.add_step(tester.deleteItemStep, "/ietf-interfaces:interfaces/interface[name='eth0']")
        subscriber.add_step(subscriber.waitStep)
        subscriber2.add_step(subscriber2.waitStep)

        srd.add_step(srd.waitStep)
        tester.add_step(tester.commitStep)
        subscriber.add_step(subscriber.waitStep)
        subscriber2.add_step(subscriber2.waitStep)

        srd.add_step(srd.waitStep)
        tester.add_step(tester.waitStep)
        subscriber.add_step(subscriber.waitStep)
        subscriber2.add_step(subscriber2.waitStep)

        srd.add_step(srd.waitStep)
        tester.add_step(tester.waitStep)
        subscriber.add_step(subscriber.checkNotificationStep,
        [["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/name"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/type"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/enabled"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/description"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/address[ip='192.168.2.100']"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/address[ip='192.168.2.100']/ip"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/address[ip='192.168.2.100']/prefix-length"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/enabled"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/mtu"]])
        subscriber2.add_step(subscriber2.checkNotificationStep,
        [["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/address[ip='192.168.2.100']"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/address[ip='192.168.2.100']/ip"],
         ["DELETED", "/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/address[ip='192.168.2.100']/prefix-length"],
         ])

        srd.add_step(srd.waitStep)
        tester.add_step(tester.waitStep)
        subscriber.add_step(subscriber.cancelSubscriptionStep)
        subscriber2.add_step(subscriber2.cancelSubscriptionStep)

        srd.add_step(srd.stopDaemonStep)

        tm.add_tester(srd)
        tm.add_tester(tester)
        tm.add_tester(subscriber)
        tm.add_tester(subscriber2)
        tm.run()

if __name__ == '__main__':
    unittest.main()