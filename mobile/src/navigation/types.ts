export type RootStackParamList = {
  Bootstrap: undefined;
  ProfileHome: undefined;
  ProfileForm: { mode: 'create' | 'edit' };
  PhotoManagement: undefined;
  Verification: undefined;
  Conversations: undefined;
  ConversationDetail: { conversationId: string };
};
