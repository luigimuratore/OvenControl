import copy
import unittest
from test_telegram_tool import preview


class TelegramUsersPreviewTest(unittest.TestCase):
    def device(self):
        return preview.DemoDevice('full',speed=1)

    def call(self,d,action,chat='0',mode='sintetica'):
        return d.request('POST','telegram/users',body=dict(action=action,chatId=chat,mode=mode))

    def test_requests_need_approval_and_removal_revokes_access(self):
        d=self.device();self.call(d,'simulateRequest')
        status=d.request('GET','telegram/users');self.assertEqual(len(status['users']),1)
        guest=status['pending'][0]['chatId']
        status=self.call(d,'approve',guest,'dettagliata')
        self.assertEqual(len(status['users']),2);self.assertEqual(status['pending'],[])
        self.assertEqual(status['users'][1]['mode'],'dettagliata')
        self.assertEqual(self.call(d,'remove',guest)['users'][0]['chatId'],'1')
        with self.assertRaises(preview.ApiError):self.call(d,'approve',guest)

    def test_personal_modes_do_not_change_owner(self):
        d=self.device();guest=self.call(d,'simulateRequest')['pending'][0]['chatId']
        self.call(d,'approve',guest)
        status=self.call(d,'mode',guest,'essenziale')
        self.assertEqual(status['users'][0]['mode'],'sintetica')
        self.assertEqual(status['users'][1]['mode'],'essenziale')
        before=copy.deepcopy(status['users'])
        for action,chat,mode in [('remove','1','sintetica'),('mode',guest,'invalid'),('mode','999','sintetica')]:
            with self.assertRaises(preview.ApiError):self.call(d,action,chat,mode)
            self.assertEqual(d.telegram_users,before)

    def test_expiry_rejection_and_capacity(self):
        d=self.device();guest=self.call(d,'simulateRequest')['pending'][0]['chatId']
        self.call(d,'reject',guest)
        with self.assertRaises(preview.ApiError):self.call(d,'approve',guest)
        guest=self.call(d,'simulateRequest')['pending'][0]['chatId'];d.now+=600
        with self.assertRaises(preview.ApiError):self.call(d,'approve',guest)
        for _ in range(7):
            guest=self.call(d,'simulateRequest')['pending'][0]['chatId'];self.call(d,'approve',guest)
        guest=self.call(d,'simulateRequest')['pending'][0]['chatId']
        with self.assertRaises(preview.ApiError):self.call(d,'approve',guest)
        self.assertEqual(len(d.telegram_users),8)

    def test_management_does_not_change_the_active_oven(self):
        d=self.device();d.temperature=50
        d.recipes=[dict(id='hold',name='Prova',steps=[dict(type='hold',target=50,duration=5)])]
        d.request('POST','command',body=dict(action='start',recipeId='hold',hardwareReady=True))
        guest=self.call(d,'simulateRequest')['pending'][0]['chatId']
        self.call(d,'approve',guest);self.call(d,'remove',guest)
        self.assertEqual(d.phase,'running');self.assertEqual(d.target,50)


if __name__=='__main__':unittest.main()
