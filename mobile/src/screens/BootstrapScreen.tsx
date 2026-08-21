import { useEffect, useState } from 'react';
import { ActivityIndicator, StyleSheet, Text, View } from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { ApiError } from '../api/client';
import { getMyProfile } from '../api/profile';
import type { RootStackParamList } from '../navigation/types';

type Props = NativeStackScreenProps<RootStackParamList, 'Bootstrap'>;

// Decides where to land the user on launch: straight into profile creation
// if they have no profile yet, or the profile hub if they do.
export default function BootstrapScreen({ navigation }: Props) {
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    getMyProfile()
      .then(() => {
        if (!cancelled) navigation.replace('ProfileHome');
      })
      .catch((err) => {
        if (cancelled) return;
        if (err instanceof ApiError && err.status === 404) {
          navigation.replace('ProfileForm', { mode: 'create' });
          return;
        }
        setError(err instanceof Error ? err.message : 'Failed to load profile.');
      });

    return () => {
      cancelled = true;
    };
  }, [navigation]);

  return (
    <View style={styles.container}>
      {error ? <Text style={styles.error}>{error}</Text> : <ActivityIndicator size="large" />}
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  error: { color: '#b00020', textAlign: 'center', paddingHorizontal: 24 },
});
