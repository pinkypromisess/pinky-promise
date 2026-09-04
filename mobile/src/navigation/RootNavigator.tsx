import { NavigationContainer } from '@react-navigation/native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';

import BootstrapScreen from '../screens/BootstrapScreen';
import ConversationDetailScreen from '../screens/ConversationDetailScreen';
import ConversationListScreen from '../screens/ConversationListScreen';
import PhotoManagementScreen from '../screens/PhotoManagementScreen';
import ProfileFormScreen from '../screens/ProfileFormScreen';
import ProfileHomeScreen from '../screens/ProfileHomeScreen';
import VerificationScreen from '../screens/VerificationScreen';
import type { RootStackParamList } from './types';

const Stack = createNativeStackNavigator<RootStackParamList>();

export default function RootNavigator() {
  return (
    <NavigationContainer>
      <Stack.Navigator initialRouteName="Bootstrap">
        <Stack.Screen name="Bootstrap" component={BootstrapScreen} options={{ headerShown: false }} />
        <Stack.Screen name="ProfileHome" component={ProfileHomeScreen} options={{ title: 'Your Profile' }} />
        <Stack.Screen name="ProfileForm" component={ProfileFormScreen} />
        <Stack.Screen
          name="PhotoManagement"
          component={PhotoManagementScreen}
          options={{ title: 'Manage Photos' }}
        />
        <Stack.Screen name="Verification" component={VerificationScreen} options={{ title: 'Verify' }} />
        <Stack.Screen name="Conversations" component={ConversationListScreen} options={{ title: 'Conversations' }} />
        <Stack.Screen name="ConversationDetail" component={ConversationDetailScreen} />
      </Stack.Navigator>
    </NavigationContainer>
  );
}
