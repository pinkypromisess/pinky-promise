import { useEffect, useState } from 'react';
import {
  ActivityIndicator,
  Button,
  Image,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { ApiError } from '../api/client';
import { getMyProfile, putProfile } from '../api/profile';
import type { Profile, RelationshipStatus, Sex } from '../api/types';
import ChoiceField from '../components/ChoiceField';
import PhotoPicker, { PickedPhoto } from '../components/PhotoPicker';
import type { RootStackParamList } from '../navigation/types';
import { isPhotoCountValid } from '../utils/photoGate';

type Props = NativeStackScreenProps<RootStackParamList, 'ProfileForm'>;

const SEX_OPTIONS: { value: Sex; label: string }[] = [
  { value: 'male', label: 'Male' },
  { value: 'female', label: 'Female' },
  { value: 'non_binary', label: 'Non-binary' },
  { value: 'other', label: 'Other' },
];

const RELATIONSHIP_OPTIONS: { value: RelationshipStatus; label: string }[] = [
  { value: 'single', label: 'Single' },
  { value: 'married', label: 'Married' },
  { value: 'married_open', label: 'Married (open)' },
  { value: 'married_separated', label: 'Married (separated)' },
  { value: 'divorced', label: 'Divorced' },
  { value: 'widowed', label: 'Widowed' },
];

export default function ProfileFormScreen({ navigation, route }: Props) {
  const { mode } = route.params;

  const [loadingExisting, setLoadingExisting] = useState(mode === 'edit');
  const [existingProfile, setExistingProfile] = useState<Profile | null>(null);

  const [name, setName] = useState('');
  const [sex, setSex] = useState<Sex | null>(null);
  const [age, setAge] = useState('');
  const [needToKnowText, setNeedToKnowText] = useState('');
  const [occupation, setOccupation] = useState('');
  const [relationshipStatus, setRelationshipStatus] = useState<RelationshipStatus | null>(null);
  const [photos, setPhotos] = useState<PickedPhoto[]>([]);

  const [submitting, setSubmitting] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  useEffect(() => {
    navigation.setOptions({ title: mode === 'edit' ? 'Edit Profile' : 'Create Profile' });
  }, [mode, navigation]);

  useEffect(() => {
    if (mode !== 'edit') return;
    let cancelled = false;
    getMyProfile()
      .then((profile) => {
        if (cancelled) return;
        setExistingProfile(profile);
        setName(profile.name);
        setSex(profile.sex);
        setAge(String(profile.age));
        setNeedToKnowText(profile.need_to_know_text);
        setOccupation(profile.occupation ?? '');
        setRelationshipStatus(profile.relationship_status);
      })
      .catch((err) => {
        if (!cancelled) setErrorMessage(err instanceof Error ? err.message : 'Failed to load profile.');
      })
      .finally(() => {
        if (!cancelled) setLoadingExisting(false);
      });
    return () => {
      cancelled = true;
    };
  }, [mode]);

  const ageNumber = Number(age);
  const ageValid = age.trim().length > 0 && Number.isInteger(ageNumber) && ageNumber >= 18 && ageNumber <= 120;

  const createPhotosReady = photos.length > 0 && photos.every((p) => !p.uploading && p.objectUrl);
  const photosSatisfied =
    mode === 'edit' ? !!existingProfile && isPhotoCountValid(existingProfile.photos.length) : isPhotoCountValid(photos.length) && createPhotosReady;

  const canSubmit =
    !submitting &&
    !loadingExisting &&
    name.trim().length > 0 &&
    !!sex &&
    ageValid &&
    needToKnowText.trim().length > 0 &&
    photosSatisfied;

  const handleSubmit = async () => {
    if (!canSubmit || !sex) return;
    setSubmitting(true);
    setErrorMessage(null);

    const photoUrls =
      mode === 'edit' && existingProfile
        ? existingProfile.photos.slice().sort((a, b) => a.position - b.position).map((p) => p.url)
        : photos.map((p) => p.objectUrl as string);

    try {
      await putProfile({
        name: name.trim(),
        sex,
        age: ageNumber,
        need_to_know_text: needToKnowText.trim(),
        photo_urls: photoUrls,
        occupation: occupation.trim().length > 0 ? occupation.trim() : null,
        relationship_status: relationshipStatus,
      });
      navigation.replace('ProfileHome');
    } catch (err) {
      if (err instanceof ApiError) {
        setErrorMessage(err.body.message);
      } else {
        setErrorMessage(err instanceof Error ? err.message : 'Failed to save profile.');
      }
    } finally {
      setSubmitting(false);
    }
  };

  if (loadingExisting) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  return (
    <ScrollView contentContainerStyle={styles.container}>
      {errorMessage && <Text style={styles.error}>{errorMessage}</Text>}

      <Text style={styles.fieldLabel}>Name</Text>
      <TextInput style={styles.input} value={name} onChangeText={setName} placeholder="Your name" />

      <ChoiceField label="Sex" options={SEX_OPTIONS} value={sex} onChange={setSex} />

      <Text style={styles.fieldLabel}>Age</Text>
      <TextInput
        style={styles.input}
        value={age}
        onChangeText={setAge}
        placeholder="Your age"
        keyboardType="number-pad"
      />

      <Text style={styles.fieldLabel}>One thing you absolutely need to know about me</Text>
      <TextInput
        style={[styles.input, styles.multiline]}
        value={needToKnowText}
        onChangeText={setNeedToKnowText}
        placeholder="What should someone know before meeting you?"
        multiline
        maxLength={500}
      />

      <Text style={styles.fieldLabel}>Occupation (optional)</Text>
      <TextInput style={styles.input} value={occupation} onChangeText={setOccupation} placeholder="Occupation" />

      <ChoiceField
        label="Relationship status (optional)"
        options={RELATIONSHIP_OPTIONS}
        value={relationshipStatus}
        onChange={setRelationshipStatus}
        allowClear
      />

      {mode === 'create' ? (
        <PhotoPicker photos={photos} onChange={setPhotos} />
      ) : (
        existingProfile && (
          <View style={styles.photoSummary}>
            <Text style={styles.fieldLabel}>Photos</Text>
            <View style={styles.photoRow}>
              {existingProfile.photos
                .slice()
                .sort((a, b) => a.position - b.position)
                .map((photo) => (
                  <Image key={photo.id} source={{ uri: photo.url }} style={styles.photoThumb} />
                ))}
            </View>
            <Text style={styles.photoHint}>
              {existingProfile.photos.length} photos. Add, replace, or remove photos on the Manage Photos screen.
            </Text>
            <Button title="Manage Photos" onPress={() => navigation.navigate('PhotoManagement')} />
          </View>
        )
      )}

      <View style={styles.submitWrap}>
        <Button
          title={submitting ? 'Saving...' : mode === 'edit' ? 'Save Changes' : 'Create Profile'}
          onPress={handleSubmit}
          disabled={!canSubmit}
        />
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { padding: 20, paddingBottom: 48 },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  fieldLabel: { fontSize: 14, fontWeight: '600', marginBottom: 6, marginTop: 4 },
  input: {
    borderWidth: 1,
    borderColor: '#ccc',
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 10,
    marginBottom: 16,
    fontSize: 16,
  },
  multiline: { minHeight: 80, textAlignVertical: 'top' },
  error: { color: '#b00020', marginBottom: 16 },
  photoSummary: { marginBottom: 16 },
  photoRow: { flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginBottom: 8 },
  photoThumb: { width: 60, height: 60, borderRadius: 8 },
  photoHint: { color: '#666', marginBottom: 8 },
  submitWrap: { marginTop: 8 },
});
