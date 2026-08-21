import { useEffect, useState } from 'react';
import * as ImagePicker from 'expo-image-picker';
import {
  ActivityIndicator,
  Button,
  Image,
  ScrollView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { ApiError } from '../api/client';
import { uploadPhoto } from '../api/photoUpload';
import { getMyProfile, patchProfilePhotos } from '../api/profile';
import type { Photo } from '../api/types';
import type { RootStackParamList } from '../navigation/types';
import { isPhotoCountValid } from '../utils/photoGate';

type Props = NativeStackScreenProps<RootStackParamList, 'PhotoManagement'>;

interface PendingPhoto {
  key: string;
  localUri: string;
  objectUrl: string | null;
  uploading: boolean;
}

export default function PhotoManagementScreen({ navigation }: Props) {
  const [loading, setLoading] = useState(true);
  const [existingPhotos, setExistingPhotos] = useState<Photo[]>([]);
  const [pendingRemoveIds, setPendingRemoveIds] = useState<Set<string>>(new Set());
  const [pendingAdd, setPendingAdd] = useState<PendingPhoto[]>([]);
  const [saving, setSaving] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [justSaved, setJustSaved] = useState(false);

  useEffect(() => {
    let cancelled = false;
    getMyProfile()
      .then((profile) => {
        if (!cancelled) setExistingPhotos(profile.photos.slice().sort((a, b) => a.position - b.position));
      })
      .catch((err) => {
        if (!cancelled) setErrorMessage(err instanceof Error ? err.message : 'Failed to load photos.');
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const toggleRemove = (id: string) => {
    setPendingRemoveIds((current) => {
      const next = new Set(current);
      if (next.has(id)) {
        next.delete(id);
      } else {
        next.add(id);
      }
      return next;
    });
  };

  const addLocalPhoto = async (localUri: string) => {
    const key = `${Date.now()}-${Math.random()}`;
    setPendingAdd((current) => [...current, { key, localUri, objectUrl: null, uploading: true }]);
    try {
      const uploaded = await uploadPhoto(localUri);
      setPendingAdd((current) =>
        current.map((p) => (p.key === key ? { ...p, objectUrl: uploaded.objectUrl, uploading: false } : p)),
      );
    } catch {
      setPendingAdd((current) => current.filter((p) => p.key !== key));
    }
  };

  const pickFromLibrary = async () => {
    const permission = await ImagePicker.requestMediaLibraryPermissionsAsync();
    if (!permission.granted) return;
    const result = await ImagePicker.launchImageLibraryAsync({ mediaTypes: ['images'], quality: 0.8 });
    if (!result.canceled && result.assets[0]) {
      await addLocalPhoto(result.assets[0].uri);
    }
  };

  const takePhoto = async () => {
    const permission = await ImagePicker.requestCameraPermissionsAsync();
    if (!permission.granted) return;
    const result = await ImagePicker.launchCameraAsync({ quality: 0.8 });
    if (!result.canceled && result.assets[0]) {
      await addLocalPhoto(result.assets[0].uri);
    }
  };

  const removePendingAdd = (key: string) => {
    setPendingAdd((current) => current.filter((p) => p.key !== key));
  };

  const remainingCount = existingPhotos.filter((p) => !pendingRemoveIds.has(p.id)).length + pendingAdd.length;
  const hasChanges = pendingRemoveIds.size > 0 || pendingAdd.length > 0;
  const addsReady = pendingAdd.every((p) => !p.uploading && p.objectUrl);
  const canSave = hasChanges && addsReady && isPhotoCountValid(remainingCount) && !saving;

  const handleSave = async () => {
    if (!canSave) return;
    setSaving(true);
    setErrorMessage(null);
    try {
      const updated = await patchProfilePhotos({
        add: pendingAdd.map((p) => p.objectUrl as string),
        remove: Array.from(pendingRemoveIds),
      });
      setExistingPhotos(updated.photos.slice().sort((a, b) => a.position - b.position));
      setPendingAdd([]);
      setPendingRemoveIds(new Set());
      setJustSaved(true);
    } catch (err) {
      if (err instanceof ApiError) {
        setErrorMessage(err.body.message);
      } else {
        setErrorMessage(err instanceof Error ? err.message : 'Failed to update photos.');
      }
    } finally {
      setSaving(false);
    }
  };

  if (loading) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  return (
    <ScrollView contentContainerStyle={styles.container}>
      {errorMessage && <Text style={styles.error}>{errorMessage}</Text>}

      {justSaved && (
        <View testID="verification-warning" style={styles.warningBanner}>
          <Text style={styles.warningTitle}>Not verified</Text>
          <Text style={styles.warningText}>
            Your photos changed, so you'll need to verify again before sending Hearts or posting a Proposal.
            Your conversations and confirmed Pinky Promises aren't affected.
          </Text>
          <Button title="Verify Now" onPress={() => navigation.navigate('Verification')} />
        </View>
      )}

      <Text testID="photo-count" style={[styles.count, isPhotoCountValid(remainingCount) ? styles.countValid : styles.countInvalid]}>
        {remainingCount} / 6 minimum
      </Text>

      <View style={styles.grid}>
        {existingPhotos.map((photo) => {
          const marked = pendingRemoveIds.has(photo.id);
          return (
            <TouchableOpacity
              key={photo.id}
              accessibilityRole="button"
              accessibilityLabel={marked ? 'Undo remove' : 'Mark for removal'}
              style={styles.thumbWrap}
              onPress={() => toggleRemove(photo.id)}
            >
              <Image source={{ uri: photo.url }} style={[styles.thumb, marked && styles.thumbMarked]} />
              <View style={styles.badge}>
                <Text style={styles.badgeText}>{marked ? 'Undo' : 'Remove'}</Text>
              </View>
            </TouchableOpacity>
          );
        })}
        {pendingAdd.map((photo) => (
          <View key={photo.key} style={styles.thumbWrap}>
            <Image source={{ uri: photo.localUri }} style={styles.thumb} />
            {photo.uploading && (
              <View style={styles.uploadingOverlay}>
                <ActivityIndicator color="#fff" />
              </View>
            )}
            <TouchableOpacity
              accessibilityRole="button"
              accessibilityLabel="Remove new photo"
              style={styles.badge}
              onPress={() => removePendingAdd(photo.key)}
            >
              <Text style={styles.badgeText}>Cancel</Text>
            </TouchableOpacity>
          </View>
        ))}
      </View>

      <View style={styles.actions}>
        <TouchableOpacity accessibilityRole="button" style={styles.actionButton} onPress={takePhoto}>
          <Text style={styles.actionText}>Take Photo</Text>
        </TouchableOpacity>
        <TouchableOpacity accessibilityRole="button" style={styles.actionButton} onPress={pickFromLibrary}>
          <Text style={styles.actionText}>Choose from Library</Text>
        </TouchableOpacity>
      </View>

      <View style={styles.saveWrap}>
        <Button title={saving ? 'Saving...' : 'Save Changes'} onPress={handleSave} disabled={!canSave} />
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { padding: 20, paddingBottom: 48 },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  error: { color: '#b00020', marginBottom: 16 },
  warningBanner: {
    backgroundColor: '#fdecea',
    borderRadius: 8,
    padding: 12,
    marginBottom: 16,
  },
  warningTitle: { fontWeight: '700', color: '#b00020', marginBottom: 4 },
  warningText: { color: '#7a1a1a', marginBottom: 8 },
  count: { marginBottom: 12, fontSize: 14, fontWeight: '600' },
  countValid: { color: '#1e7e34' },
  countInvalid: { color: '#b00020' },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginBottom: 16 },
  thumbWrap: { width: 80, height: 80, borderRadius: 8, overflow: 'hidden', position: 'relative' },
  thumb: { width: '100%', height: '100%' },
  thumbMarked: { opacity: 0.3 },
  uploadingOverlay: {
    ...StyleSheet.absoluteFill,
    backgroundColor: 'rgba(0,0,0,0.4)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  badge: {
    position: 'absolute',
    bottom: 0,
    left: 0,
    right: 0,
    backgroundColor: 'rgba(0,0,0,0.6)',
    paddingVertical: 2,
    alignItems: 'center',
  },
  badgeText: { color: '#fff', fontSize: 11, fontWeight: '600' },
  actions: { flexDirection: 'row', gap: 12, marginBottom: 20 },
  actionButton: {
    flex: 1,
    paddingVertical: 10,
    borderRadius: 8,
    borderWidth: 1,
    borderColor: '#2f6feb',
    alignItems: 'center',
  },
  actionText: { color: '#2f6feb', fontWeight: '600' },
  saveWrap: { marginTop: 4 },
});
