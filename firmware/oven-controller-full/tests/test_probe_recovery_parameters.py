import unittest
from unittest.mock import patch
from test_telegram_tool import preview


class ProbeRecoveryParametersTest(unittest.TestCase):
    def test_default_save_and_later_status(self):
        device=preview.DemoDevice('full',speed=1)
        self.assertEqual(device.status()['sensorRecovery']['bothFaultTimeoutSec'],20)
        for limit in (1,3,20,300):
            reply=device.request('POST','sensor-recovery',body=dict(bothFaultTimeoutSec=limit))
            self.assertEqual(reply['sensorRecovery'],dict(bothFaultTimeoutSec=limit,minAllowed=1,maxAllowed=300,validProbes=2,waiting=False,missingForMs=0))
            self.assertEqual(device.request('GET','status')['sensorRecovery']['bothFaultTimeoutSec'],limit)

    def test_invalid_payload_never_replaces_applied_limit(self):
        device=preview.DemoDevice('full',speed=1)
        device.request('POST','sensor-recovery',body=dict(bothFaultTimeoutSec=3))
        for value in (None,True,False,0,-1,301,1000,2.5,'10',float('nan')):
            with self.subTest(value=value):
                with self.assertRaises(preview.ApiError):
                    device.request('POST','sensor-recovery',body=dict(bothFaultTimeoutSec=value))
                self.assertEqual(device.status()['sensorRecovery']['bothFaultTimeoutSec'],3)

    def test_changes_blocked_during_program_manual_pause_test_and_ota(self):
        for mode in ('program','manual','pause','test','ota'):
            with self.subTest(mode=mode):
                device=preview.DemoDevice('full',speed=1)
                if mode in ('program','pause'):
                    device.request('POST','command',body=dict(action='start',recipeId=device.recipes[0]['id'],hardwareReady=True))
                    if mode=='pause': device.request('POST','command',body=dict(action='pause'))
                elif mode=='manual': device.request('POST','manual',body=dict(action='start',targetC=50,rateCMin=1,hardwareReady=True))
                elif mode=='test': device.request('POST','test/command',body=dict(action='enter'))
                if mode=='ota':
                    with patch.object(device,'status',return_value=dict(otaUpdating=True)):
                        with self.assertRaises(preview.ApiError): device.request('POST','sensor-recovery',body=dict(bothFaultTimeoutSec=3))
                else:
                    with self.assertRaises(preview.ApiError): device.request('POST','sensor-recovery',body=dict(bothFaultTimeoutSec=3))
                self.assertEqual(device.both_fault_timeout_sec,20)


if __name__=='__main__': unittest.main()
