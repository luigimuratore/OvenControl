import copy
import unittest
from unittest.mock import patch
from test_telegram_tool import preview


class ParametersPreviewTest(unittest.TestCase):
    def device(self,mode='program',settings=None):
        d=preview.DemoDevice('full',speed=1);d.temperature=175
        if settings:d.request('POST','protections',body={**d.protections,**settings})
        if mode.startswith('manual'):
            d.request('POST','manual',body=dict(action='start',targetC=175,rateCMin=1,hardwareReady=True))
            if mode=='manual-cooldown':d.request('POST','manual',body=dict(action='cooldown',targetC=25,rateCMin=1))
        else:
            d.recipes=[dict(id='param',name='Blocchi',steps=[dict(type='cooldown' if mode=='cooldown' else 'hold',target=25 if mode=='cooldown' else 175,rate=1,duration=5)])]
            d.request('POST','command',body=dict(action='start',recipeId='param',hardwareReady=True))
            if mode=='pause':d.request('POST','command',body=dict(action='pause'))
        d.target=175
        return d

    def sample(self,d,first,second):
        pair=d.sensors();pair[0]['c']=first;pair[1]['c']=second
        with patch.object(d,'sensors',return_value=pair):d.advance_full(0)

    def test_defaults_and_independent_thresholds_in_all_thermal_modes(self):
        modes=('program','pause','manual','cooldown','manual-cooldown')
        for mode in modes:
            for first,second,alarm in ((160,190,False),(160,190.01,True),(185,185,False),(185.01,185.01,True),(160,160,False)):
                with self.subTest(mode=mode,first=first,second=second):
                    d=self.device(mode);self.sample(d,first,second)
                    self.assertEqual(d.phase,'fault' if alarm else 'paused' if mode=='pause' else 'running')
                    if alarm:self.assertFalse(d.relay);self.assertEqual(d.duty,0)

    def test_each_toggle_and_custom_threshold_act_independently(self):
        for settings,first,second,fault in ((dict(probeDeltaEnabled=False),150,200,False),
                (dict(probeDeltaEnabled=False),190,190,True),
                (dict(meanOvershootEnabled=False),190,190,False),
                (dict(meanOvershootEnabled=False),150,200,True),
                (dict(maxProbeDeltaC=40),160,190,False),
                (dict(maxProbeDeltaC=20),160,190,True),
                (dict(maxMeanOvershootC=5),181,181,True),
                (dict(maxMeanOvershootC=20),190,190,False)):
            with self.subTest(settings=settings):
                d=self.device(settings=settings);self.sample(d,first,second)
                self.assertEqual(d.phase,'fault' if fault else 'running')

    def test_hard_cutoff_and_probe_fault_remain_with_both_guards_disabled(self):
        for kind in ('absolute','probe'):
            d=self.device(settings=dict(probeDeltaEnabled=False,meanOvershootEnabled=False))
            if kind=='absolute':self.sample(d,415,100)
            else:d.request('POST','preview-sensor',body=dict(missing=True))
            self.assertEqual(d.phase,'fault');self.assertFalse(d.relay)

    def test_cannot_save_during_active_program_manual_pause_or_test(self):
        for mode in ('program','manual','pause'):
            d=self.device(mode);before=copy.deepcopy(d.protections)
            with self.assertRaises(preview.ApiError):d.request('POST','protections',body={**before,'probeDeltaEnabled':False})
            self.assertEqual(d.protections,before)
        d=preview.DemoDevice('full');d.request('POST','test/command',body=dict(action='enter'))
        with self.assertRaises(preview.ApiError):d.request('POST','protections',body=d.protections)

    def test_invalid_configuration_does_not_replace_saved_values(self):
        d=preview.DemoDevice('full');before=copy.deepcopy(d.protections)
        for key,bad in [('probeDeltaEnabled','false'),('meanOvershootEnabled',1),('maxProbeDeltaC',0),
                        ('maxProbeDeltaC',None),('maxMeanOvershootC',float('nan')),('maxMeanOvershootC',431)]:
            with self.subTest(key=key,bad=bad):
                with self.assertRaises(preview.ApiError):d.request('POST','protections',body={**before,key:bad})
                self.assertEqual(d.protections,before)

    def test_updated_guard_applies_to_explicit_continuation(self):
        d=self.device();self.sample(d,150,200)
        self.assertEqual(d.phase,'fault');self.assertIsNotNone(d.fault_resume)
        pair=d.sensors();pair[0]['c']=150;pair[1]['c']=200
        with patch.object(d,'sensors',return_value=pair):
            self.assertFalse(d.status()['faultResume']['ready'])
            d.request('POST','protections',body={**d.protections,'probeDeltaEnabled':False})
            self.assertTrue(d.status()['faultResume']['ready'])
            d.request('POST','command',body=dict(action='resetContinue'))
        self.assertEqual(d.phase,'running')


if __name__=='__main__':unittest.main()
