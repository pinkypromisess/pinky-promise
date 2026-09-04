import type { ReactNode } from 'react';
import { Image, StyleSheet, Text, View } from 'react-native';

import type { ProposalFeedItem } from '../api/proposals';

interface Props {
  item: ProposalFeedItem;
}

const PAYMENT_LABELS: Record<string, string> = {
  split: "Let's split it",
  host_treats: "I'm treating",
  guest_treats: "You're treating",
  tbd: "We'll figure it out",
};

function formatEventTime(iso: string): string {
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return iso;
  return date.toLocaleString(undefined, {
    weekday: 'short',
    month: 'short',
    day: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
  });
}

// Renders one Proposal as a single continuous vertical scroll where photos
// and info blocks alternate (photo -> info -> photo -> info -> ...), per
// CUJ #3's confirmed card layout -- deliberately not a separate photo
// carousel plus a text section. This component only renders the scrolling
// content; the host screen owns the ScrollView wrapper and the sticky
// X/Heart action bar below it, since those must stay fixed across cards.
//
// Field grouping into info blocks isn't specified beyond "alternate" being
// the hard requirement, so fields are split into several small blocks (one
// per topic) rather than one big text dump -- with the 6-photo minimum from
// CUJ #1, this gives a true photo/info/photo/info rhythm for a typical card
// instead of a few blocks up front trailed by bare photos.
export default function ProposalCard({ item }: Props) {
  const { proposal, creator } = item;
  const photos = creator.photos.slice().sort((a, b) => a.position - b.position);

  const infoBlocks: ReactNode[] = [
    <View key="intro" style={styles.infoBlock}>
      <Text style={styles.name}>
        {creator.name}
        {typeof creator.age === 'number' ? `, ${creator.age}` : ''}
      </Text>
      <Text style={styles.meta}>{creator.sex}</Text>
    </View>,
    <View key="need-to-know" style={styles.infoBlock}>
      <Text style={styles.label}>Need to know</Text>
      <Text style={styles.value}>{creator.need_to_know_text}</Text>
    </View>,
    <View key="activity" style={styles.infoBlock}>
      <Text style={styles.label}>Would love to</Text>
      <Text style={styles.value}>{proposal.activity_text}</Text>
      <Text style={styles.label}>When</Text>
      <Text style={styles.value}>{formatEventTime(proposal.event_time)}</Text>
    </View>,
    <View key="location" style={styles.infoBlock}>
      <Text style={styles.label}>Where</Text>
      <Text style={styles.value}>{proposal.location.address}</Text>
    </View>,
    <View key="logistics" style={styles.infoBlock}>
      <Text style={styles.label}>Payment</Text>
      <Text style={styles.value}>{PAYMENT_LABELS[proposal.payment_type] ?? proposal.payment_type}</Text>
      <Text style={styles.label}>Looking for</Text>
      <Text style={styles.value}>{proposal.looking_for_text}</Text>
    </View>,
  ];

  if (creator.occupation || creator.relationship_status) {
    infoBlocks.push(
      <View key="extras" style={styles.infoBlock}>
        {creator.occupation && (
          <>
            <Text style={styles.label}>Occupation</Text>
            <Text style={styles.value}>{creator.occupation}</Text>
          </>
        )}
        {creator.relationship_status && (
          <>
            <Text style={styles.label}>Relationship status</Text>
            <Text style={styles.value}>{creator.relationship_status.replace('_', ' ')}</Text>
          </>
        )}
      </View>,
    );
  }

  // Interleave: photo, block, photo, block, ... Once one side runs out,
  // append whatever remains of the other (still a single vertical scroll,
  // just without further alternation for the tail).
  const rows: ReactNode[] = [];
  const maxLen = Math.max(photos.length, infoBlocks.length);
  for (let i = 0; i < maxLen; i += 1) {
    if (photos[i]) {
      rows.push(<Image key={`photo-${photos[i].id}`} testID="proposal-photo" source={{ uri: photos[i].url }} style={styles.photo} />);
    }
    if (infoBlocks[i]) {
      rows.push(infoBlocks[i]);
    }
  }

  return (
    <View testID="proposal-card" style={styles.card}>
      {rows}
    </View>
  );
}

const styles = StyleSheet.create({
  card: { width: '100%' },
  photo: { width: '100%', aspectRatio: 1, backgroundColor: '#e0e0e0' },
  infoBlock: { padding: 16, gap: 4 },
  name: { fontSize: 22, fontWeight: '700' },
  meta: { color: '#666', textTransform: 'capitalize' },
  label: { marginTop: 8, fontSize: 12, fontWeight: '700', color: '#888', textTransform: 'uppercase' },
  value: { fontSize: 16 },
});
