import * as ImagePicker from 'expo-image-picker';
import type { Dispatch, SetStateAction } from 'react';
import { ActivityIndicator, Image, StyleSheet, Text, TouchableOpacity, View } from 'react-native';

import { uploadPhoto } from '../api/photoUpload';
import { MIN_PROFILE_PHOTOS, isPhotoCountValid } from '../utils/photoGate';

export interface PickedPhoto {
  key: string;
  localUri: string;
  objectUrl: string | null;
  uploading: boolean;
}

interface Props {
  photos: PickedPhoto[];
  onChange: Dispatch<SetStateAction<PickedPhoto[]>>;
}

export default function PhotoPicker({ photos, onChange }: Props) {
  const addPhoto = async (localUri: string) => {
    const key = `${Date.now()}-${Math.random()}`;
    onChange((current) => [...current, { key, localUri, objectUrl: null, uploading: true }]);

    try {
      const uploaded = await uploadPhoto(localUri);
      onChange((current) =>
        current.map((p) => (p.key === key ? { ...p, objectUrl: uploaded.objectUrl, uploading: false } : p)),
      );
    } catch {
      onChange((current) => current.filter((p) => p.key !== key));
    }
  };

  const pickFromLibrary = async () => {
    const permission = await ImagePicker.requestMediaLibraryPermissionsAsync();
    if (!permission.granted) return;

    const result = await ImagePicker.launchImageLibraryAsync({
      mediaTypes: ['images'],
      quality: 0.8,
    });
    if (!result.canceled && result.assets[0]) {
      await addPhoto(result.assets[0].uri);
    }
  };

  const takePhoto = async () => {
    const permission = await ImagePicker.requestCameraPermissionsAsync();
    if (!permission.granted) return;

    const result = await ImagePicker.launchCameraAsync({ quality: 0.8 });
    if (!result.canceled && result.assets[0]) {
      await addPhoto(result.assets[0].uri);
    }
  };

  const removePhoto = (key: string) => {
    onChange((current) => current.filter((p) => p.key !== key));
  };

  const valid = isPhotoCountValid(photos.length);

  return (
    <View style={styles.container}>
      <Text style={styles.label}>Photos</Text>
      <Text testID="photo-count" style={[styles.count, valid ? styles.countValid : styles.countInvalid]}>
        {photos.length} / {MIN_PROFILE_PHOTOS} minimum
      </Text>

      <View style={styles.grid}>
        {photos.map((photo) => (
          <View key={photo.key} style={styles.thumbWrap}>
            <Image source={{ uri: photo.localUri }} style={styles.thumb} />
            {photo.uploading && (
              <View style={styles.uploadingOverlay}>
                <ActivityIndicator color="#fff" />
              </View>
            )}
            <TouchableOpacity
              accessibilityRole="button"
              accessibilityLabel="Remove photo"
              style={styles.removeBadge}
              onPress={() => removePhoto(photo.key)}
            >
              <Text style={styles.removeBadgeText}>×</Text>
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
    </View>
  );
}

const styles = StyleSheet.create({
  container: { marginBottom: 16 },
  label: { fontSize: 14, fontWeight: '600', marginBottom: 4 },
  count: { marginBottom: 8, fontSize: 13 },
  countValid: { color: '#1e7e34' },
  countInvalid: { color: '#b00020' },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginBottom: 12 },
  thumbWrap: { width: 80, height: 80, borderRadius: 8, overflow: 'hidden', position: 'relative' },
  thumb: { width: '100%', height: '100%' },
  uploadingOverlay: {
    ...StyleSheet.absoluteFill,
    backgroundColor: 'rgba(0,0,0,0.4)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  removeBadge: {
    position: 'absolute',
    top: 2,
    right: 2,
    width: 20,
    height: 20,
    borderRadius: 10,
    backgroundColor: 'rgba(0,0,0,0.6)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  removeBadgeText: { color: '#fff', fontSize: 14, lineHeight: 16 },
  actions: { flexDirection: 'row', gap: 12 },
  actionButton: {
    flex: 1,
    paddingVertical: 10,
    borderRadius: 8,
    borderWidth: 1,
    borderColor: '#2f6feb',
    alignItems: 'center',
  },
  actionText: { color: '#2f6feb', fontWeight: '600' },
});
