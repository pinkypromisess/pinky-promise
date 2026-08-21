import { useCallback, useState } from 'react';
import { useFocusEffect } from '@react-navigation/native';
import { ActivityIndicator, Button, StyleSheet, Text, View } from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { getMyProfile } from '../api/profile';
import type { Profile } from '../api/types';
import type { RootStackParamList } from '../navigation/types';

type Props = NativeStackScreenProps<RootStackParamList, 'ProfileHome'>;

export default function ProfileHomeScreen({ navigation }: Props) {
  const [profile, setProfile] = useState<Profile | null>(null);
  const [loading, setLoading] = useState(true);

  useFocusEffect(
    useCallback(() => {
      let cancelled = false;
      setLoading(true);
      getMyProfile()
        .then((p) => {
          if (!cancelled) setProfile(p);
        })
        .finally(() => {
          if (!cancelled) setLoading(false);
        });
      return () => {
        cancelled = true;
      };
    }, []),
  );

  if (loading || !profile) {
    return (
      <View style={styles.container}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <Text style={styles.name}>{profile.name}</Text>
      <Text
        testID="verified-badge"
        style={[styles.badge, profile.verified ? styles.badgeVerified : styles.badgeUnverified]}
      >
        {profile.verified ? 'Verified' : 'Not verified'}
      </Text>
      <Text style={styles.meta}>{profile.photos.length} photos</Text>

      <View style={styles.actions}>
        <Button title="Edit Profile" onPress={() => navigation.navigate('ProfileForm', { mode: 'edit' })} />
        <View style={styles.spacer} />
        <Button title="Manage Photos" onPress={() => navigation.navigate('PhotoManagement')} />
        <View style={styles.spacer} />
        <Button title={profile.verified ? 'Re-verify' : 'Verify Now'} onPress={() => navigation.navigate('Verification')} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 24 },
  name: { fontSize: 24, fontWeight: '600' },
  meta: { color: '#666', marginTop: 4 },
  badge: { marginTop: 12, paddingHorizontal: 12, paddingVertical: 4, borderRadius: 12, overflow: 'hidden', fontWeight: '600' },
  badgeVerified: { backgroundColor: '#e6f4ea', color: '#1e7e34' },
  badgeUnverified: { backgroundColor: '#fdecea', color: '#b00020' },
  actions: { marginTop: 32, width: '100%' },
  spacer: { height: 12 },
});
